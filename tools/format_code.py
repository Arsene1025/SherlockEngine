"""SherlockEngine C++ 코드 포맷터.

  python tools/format_code.py            저장소의 .h/.cpp 를 고친다 (중괄호 규칙 → clang-format)
  python tools/format_code.py --check    고치지 않고 규칙에 어긋난 곳만 알린다 (어긋나면 종료 코드 1)
  python tools/format_code.py 파일...    지정한 파일만

clang-format(.clang-format) 이 들여쓰기·공백·중괄호 위치를 맡고, 이 스크립트는 clang-format 이
표현하지 못하는 중괄호 규칙을 먼저 적용한다.
  - for / while / do / else 몸통은 항상 중괄호로 감싼다.
  - if 몸통을 중괄호 없이 쓸 수 있는 것은 `if (조건) return ...;`, `continue;`, `break;` 를
    한 줄에 쓸 때뿐이다. else 가 붙은 if 는 모든 갈래를 중괄호로 감싼다.

clang-format 은 PATH, 환경 변수 CLANG_FORMAT, Visual Studio 설치(vswhere) 순서로 찾는다.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXTENSIONS = ('.h', '.hpp', '.cpp', '.inl')
JUMP_KEYWORDS = ('return', 'continue', 'break')


# ---------------------------------------------------------------------------
# 토큰 나누기: 주석·문자열·전처리 줄을 하나의 토큰으로 묶어서 그 안의 괄호나 키워드를 무시한다.
# ---------------------------------------------------------------------------
class Token:
    __slots__ = ('kind', 'text', 'start', 'end')

    def __init__(self, kind, text, start, end):
        self.kind = kind    # 'id', 'num', 'str', 'punct', 'comment', 'pp'
        self.text = text
        self.start = start
        self.end = end


RAW_PREFIXES = ('R', 'LR', 'uR', 'UR', 'u8R')
STR_PREFIXES = ('L', 'u', 'U', 'u8')


def tokenize(src):
    tokens = []
    i, n = 0, len(src)
    line_start = True   # 이 줄에서 아직 공백 말고는 나오지 않았는가 (전처리 줄 판별)
    while i < n:
        c = src[i]
        if c == '\n':
            line_start = True
            i += 1
            continue
        if c in ' \t\r\f\v':
            i += 1
            continue
        start = i
        if c == '#' and line_start:
            # 전처리 줄: 줄 끝 '\' 이어 쓰기까지 한 토큰
            while i < n:
                if src[i] == '\n':
                    if src[i - 1] == '\\' or (src[i - 1] == '\r' and src[i - 2] == '\\'):
                        i += 1
                        continue
                    break
                i += 1
            tokens.append(Token('pp', src[start:i], start, i))
            continue
        line_start = False
        if src.startswith('//', i):
            j = src.find('\n', i)
            i = n if j < 0 else j
            tokens.append(Token('comment', src[start:i], start, i))
            continue
        if src.startswith('/*', i):
            j = src.find('*/', i + 2)
            i = n if j < 0 else j + 2
            tokens.append(Token('comment', src[start:i], start, i))
            continue
        if c.isalpha() or c == '_':
            j = i
            while j < n and (src[j].isalnum() or src[j] == '_'):
                j += 1
            word = src[i:j]
            if j < n and src[j] == '"' and word in RAW_PREFIXES:
                k = src.find('(', j)
                delim = src[j + 1:k]
                end = src.find(')' + delim + '"', k)
                i = n if end < 0 else end + len(delim) + 2
                tokens.append(Token('str', src[start:i], start, i))
                continue
            if j < n and src[j] in '"\'' and word in STR_PREFIXES:
                i = skip_quoted(src, j)
                tokens.append(Token('str', src[start:i], start, i))
                continue
            i = j
            tokens.append(Token('id', word, start, i))
            continue
        if c.isdigit() or (c == '.' and i + 1 < n and src[i + 1].isdigit()):
            j = i + 1
            while j < n:
                d = src[j]
                if d.isalnum() or d in "._'":
                    j += 1
                elif d in '+-' and src[j - 1] in 'eEpP':
                    j += 1
                else:
                    break
            i = j
            tokens.append(Token('num', src[start:i], start, i))
            continue
        if c in '"\'':
            i = skip_quoted(src, i)
            tokens.append(Token('str', src[start:i], start, i))
            continue
        i += 1
        tokens.append(Token('punct', c, start, i))
    return tokens


def skip_quoted(src, i):
    quote = src[i]
    i += 1
    n = len(src)
    while i < n:
        c = src[i]
        if c == '\\':
            i += 2
            continue
        if c == quote or c == '\n':
            return i + 1
        i += 1
    return n


# ---------------------------------------------------------------------------
# 중괄호 규칙
# ---------------------------------------------------------------------------
class BraceError(Exception):
    def __init__(self, offset, message):
        super().__init__(message)
        self.offset = offset


class BraceFixer:
    """제어문 몸통을 찾아 중괄호를 넣을 자리를 모은다. 소스를 직접 고치지는 않는다."""

    def __init__(self, src):
        self.src = src
        all_tokens = tokenize(src)
        self.code = [t for t in all_tokens if t.kind not in ('comment', 'pp')]
        self.trivia = [t for t in all_tokens if t.kind in ('comment', 'pp')]
        self.pp_offsets = [t.start for t in all_tokens if t.kind == 'pp']
        self.inserts = []        # (offset, order, text)
        self.violations = []     # (offset, message)
        self.errors = []         # (offset, message) - 자동으로 고치지 못한 곳
        self.handled = set()     # 이미 처리한 제어문 키워드 토큰 번호
        self.skip_while = set()  # do-while 의 while 토큰 번호

    # --- 도우미 -------------------------------------------------------------
    def is_kw(self, i, word):
        return 0 <= i < len(self.code) and self.code[i].kind == 'id' and self.code[i].text == word

    def is_p(self, i, ch):
        return 0 <= i < len(self.code) and self.code[i].kind == 'punct' and self.code[i].text == ch

    def match(self, i):
        """code[i] 가 여는 괄호일 때 짝이 되는 닫는 괄호의 번호."""
        pairs = {'(': ')', '[': ']', '{': '}'}
        stack = []
        for j in range(i, len(self.code)):
            t = self.code[j]
            if t.kind != 'punct':
                continue
            if t.text in pairs:
                stack.append(pairs[t.text])
            elif t.text in ')]}':
                if not stack or stack[-1] != t.text:
                    raise BraceError(t.start, '괄호 짝이 맞지 않음')
                stack.pop()
                if not stack:
                    return j
        raise BraceError(self.code[i].start, '닫는 괄호가 없음')

    def line_of(self, offset):
        return self.src.count('\n', 0, offset) + 1

    def same_line(self, a, b):
        return '\n' not in self.src[a:b]

    def line_indent(self, offset):
        ls = self.src.rfind('\n', 0, offset) + 1
        width = 0
        for ch in self.src[ls:]:
            if ch == '\t':
                width += 4 - width % 4
            elif ch == ' ':
                width += 1
            else:
                break
        return width

    # --- 문장 끝 찾기 --------------------------------------------------------
    def header_end(self, i):
        """if/for/while/switch 키워드 번호 → 조건 괄호 ')' 다음 토큰 번호."""
        j = i + 1
        if self.is_kw(i, 'if') and (self.is_kw(j, 'constexpr') or self.is_kw(j, 'consteval')):
            j += 1
        if not self.is_p(j, '('):
            raise BraceError(self.code[i].start, f"'{self.code[i].text}' 다음에 '(' 가 없음")
        return self.match(j) + 1

    def statement_end(self, i):
        """code[i] 에서 시작하는 문장 하나의 마지막 토큰 번호."""
        t = self.code[i]
        if self.is_p(i, '{'):
            return self.match(i)
        if t.kind == 'id' and t.text == 'if':
            body = self.header_end(i)
            end = self.statement_end(body)
            if self.is_kw(end + 1, 'else'):
                end = self.statement_end(end + 2)
            return end
        if t.kind == 'id' and t.text in ('for', 'while', 'switch'):
            return self.statement_end(self.header_end(i))
        if t.kind == 'id' and t.text == 'do':
            end = self.statement_end(i + 1)
            if not self.is_kw(end + 1, 'while'):
                raise BraceError(t.start, "do 다음에 while 이 없음")
            close = self.match(end + 2)
            if not self.is_p(close + 1, ';'):
                raise BraceError(t.start, "do-while 끝에 ';' 가 없음")
            return close + 1
        if t.kind == 'id' and t.text in ('try', 'case', 'default'):
            raise BraceError(t.start, f"중괄호 없는 몸통이 '{t.text}' 로 시작함")
        if self.is_p(i, ';'):
            raise BraceError(t.start, '빈 문장이 몸통임')
        j = i
        while j < len(self.code):
            if self.is_p(j, ';'):
                return j
            if self.code[j].kind == 'punct' and self.code[j].text in '([{':
                j = self.match(j) + 1
                continue
            if self.code[j].kind == 'punct' and self.code[j].text in ')]}':
                break
            j += 1
        raise BraceError(t.start, "문장 끝 ';' 를 찾지 못함")

    # --- 몸통 처리 -----------------------------------------------------------
    def body(self, head, body_start, keyword, allow_unbraced):
        """head: 제어문 첫 토큰 번호, body_start: 몸통 첫 토큰 번호. 몸통 마지막 토큰 번호를 돌려준다."""
        if self.is_p(body_start, '{'):
            return self.match(body_start)
        end = self.statement_end(body_start)
        if allow_unbraced:
            return end
        first, last = self.code[body_start], self.code[end]
        head_tok = self.code[head]
        # 몸통이 여러 줄이면 이어지는 줄이 제어문보다 더 들여 써져 있어야 한다.
        # 그렇지 않으면 세미콜론 없는 매크로 같은 것 때문에 몸통을 잘못 잡았을 수 있다.
        base = self.line_indent(head_tok.start)
        for k in range(body_start, end + 1):
            tk = self.code[k]
            if not self.same_line(first.start, tk.start):
                ls = self.src.rfind('\n', 0, tk.start) + 1
                if self.src[ls:tk.start].strip() == '' and self.line_indent(tk.start) <= base:
                    raise BraceError(head_tok.start, f"'{keyword}' 몸통의 끝을 확신할 수 없음 (들여쓰기가 제어문과 같음)")
        for off in self.pp_offsets:
            if first.start < off < last.end:
                raise BraceError(head_tok.start, f"'{keyword}' 몸통 안에 전처리 줄이 있음")
        self.violations.append((head_tok.start, f"'{keyword}' 몸통에 중괄호가 없음"))
        # 여는 중괄호: 몸통 앞의 공백(줄바꿈 포함)을 줄바꿈 + '{' 로 바꾼다
        self.inserts.append((first.start, 0, '\n{\n'))
        # 닫는 중괄호: 몸통 마지막 ';' 와 같은 줄에 끝 주석이 있으면 그 뒤에 둔다
        close_at = last.end
        next_code = self.code[end + 1].start if end + 1 < len(self.code) else len(self.src)
        for tv in self.trivia:
            if tv.start < last.end:
                continue
            if tv.kind == 'comment' and tv.start < next_code and self.same_line(last.end, tv.start):
                close_at = tv.end
            break
        self.inserts.append((close_at, 1, '\n}'))
        return end

    def jump_only(self, body_start):
        return self.code[body_start].kind == 'id' and self.code[body_start].text in JUMP_KEYWORDS

    def process_if(self, i, in_chain):
        """if 키워드 번호. else 갈래까지 처리하고 마지막 토큰 번호를 돌려준다."""
        self.handled.add(i)
        body_start = self.header_end(i)
        has_else = False
        if not self.is_p(body_start, '{'):
            end = self.statement_end(body_start)
            has_else = self.is_kw(end + 1, 'else')
        else:
            has_else = self.is_kw(self.match(body_start) + 1, 'else')
        chain = in_chain or has_else
        allow = False
        if not chain and not self.is_p(body_start, '{') and self.jump_only(body_start):
            end = self.statement_end(body_start)
            allow = self.same_line(self.code[i].start, self.code[end].end)
        end = self.body(i, body_start, 'if', allow)
        if self.is_kw(end + 1, 'else'):
            e = end + 1
            self.handled.add(e)
            if self.is_kw(e + 1, 'if'):
                return self.process_if(e + 1, True)
            return self.body(e, e + 1, 'else', False)
        return end

    def run(self):
        code = self.code
        i = 0
        while i < len(code):
            t = code[i]
            if t.kind == 'id' and i not in self.handled:
                try:
                    if t.text == 'if':
                        self.process_if(i, False)
                    elif t.text in ('for', 'while') and i not in self.skip_while:
                        self.handled.add(i)
                        start = self.header_end(i)
                        if self.is_p(start, ';') and t.text == 'while':
                            pass   # do { } while (…); 의 끝은 아래 do 에서 거른다
                        else:
                            self.body(i, start, t.text, False)
                    elif t.text == 'do':
                        self.handled.add(i)
                        end = self.body(i, i + 1, 'do', False)
                        if self.is_kw(end + 1, 'while'):
                            self.skip_while.add(end + 1)
                except BraceError as e:
                    self.errors.append((e.offset, str(e)))
            i += 1
        return self

    def apply(self):
        out = self.src
        for offset, order, text in sorted(self.inserts, key=lambda x: (x[0], x[1]), reverse=True):
            if text.startswith('\n{'):
                # 몸통 앞 공백·줄바꿈을 지우고 넣는다 (주석은 남는다)
                ws = offset
                while ws > 0 and out[ws - 1] in ' \t\n':
                    ws -= 1
                out = out[:ws] + text + out[offset:]
            else:
                out = out[:offset] + text + out[offset:]
        return out


# ---------------------------------------------------------------------------
# 파일 처리
# ---------------------------------------------------------------------------
def read_source(path):
    raw = open(path, 'rb').read()
    bom = raw.startswith(b'\xef\xbb\xbf')
    text = raw[3:] if bom else raw
    return bom, text.decode('utf-8').replace('\r\n', '\n')


def write_source(path, bom, text):
    data = text.replace('\n', '\r\n').encode('utf-8')
    with open(path, 'wb') as f:
        f.write((b'\xef\xbb\xbf' if bom else b'') + data)


def find_clang_format():
    exe = os.environ.get('CLANG_FORMAT') or shutil.which('clang-format')
    if exe:
        return exe
    vswhere = os.path.join(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'),
                           'Microsoft Visual Studio', 'Installer', 'vswhere.exe')
    if os.path.exists(vswhere):
        out = subprocess.run([vswhere, '-latest', '-products', '*', '-find',
                              r'VC\Tools\Llvm\x64\bin\clang-format.exe'],
                             capture_output=True, text=True).stdout.strip().splitlines()
        if out and os.path.exists(out[0]):
            return out[0]
    sys.exit('clang-format 을 찾지 못했습니다. PATH 에 넣거나 CLANG_FORMAT 환경 변수로 경로를 알려 주세요.')


def tracked_sources():
    out = subprocess.run(['git', 'ls-files', '-z'], cwd=ROOT, capture_output=True, check=True).stdout
    files = [p for p in out.decode('utf-8').split('\0') if p.lower().endswith(EXTENSIONS)]
    return [os.path.join(ROOT, p) for p in files]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--check', action='store_true', help='고치지 않고 검사만 한다')
    ap.add_argument('--braces-only', action='store_true', help='clang-format 은 돌리지 않는다')
    ap.add_argument('files', nargs='*')
    args = ap.parse_args()
    sys.stdout.reconfigure(errors='replace')

    files = [os.path.abspath(f) for f in args.files] or tracked_sources()
    problems = 0
    for path in files:
        rel = os.path.relpath(path, ROOT)
        bom, src = read_source(path)
        fixer = BraceFixer(src).run()
        for off, msg in fixer.errors:
            print(f'{rel}:{fixer.line_of(off)}: 자동으로 고칠 수 없음 - {msg}')
            problems += 1
        if args.check:
            for off, msg in fixer.violations:
                print(f'{rel}:{fixer.line_of(off)}: {msg}')
                problems += 1
        elif fixer.inserts:
            write_source(path, bom, fixer.apply())

    if not args.braces_only:
        clang_format = find_clang_format()
        for k in range(0, len(files), 50):
            chunk = files[k:k + 50]
            if args.check:
                r = subprocess.run([clang_format, '--dry-run', '--Werror', *chunk], cwd=ROOT,
                                   capture_output=True, text=True, encoding='utf-8', errors='replace')
                if r.returncode != 0:
                    bad = sorted({m.group(1) for m in re.finditer(
                        r'^(.+?):\d+:\d+: (?:error|warning):.*clang-format-violations', r.stderr, re.M)})
                    for b in bad:
                        print(f'{os.path.relpath(os.path.join(ROOT, b), ROOT)}: clang-format 결과와 다름')
                    problems += len(bad) or 1
            else:
                # 두 번 돌린다. 한 줄 함수를 펼친 뒤의 줄 끝 주석 정렬처럼, 첫 번째 결과를
                # 다시 넣어야 자리를 잡는 경우가 있다.
                for _ in range(2):
                    subprocess.run([clang_format, '-i', *chunk], cwd=ROOT, check=True)

    if args.check:
        print('규칙에 어긋난 곳 없음' if problems == 0 else f'어긋난 곳 {problems}개')
    elif problems:
        print(f'자동으로 고치지 못한 곳 {problems}개 - 직접 고친 뒤 다시 실행하세요')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
