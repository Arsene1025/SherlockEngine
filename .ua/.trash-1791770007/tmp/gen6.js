const fs=require('fs');
const R=JSON.parse(fs.readFileSync('.ua/tmp/ua-file-extract-results-6.json','utf8')).results;
const by={};R.forEach(r=>by[r.path]=r);
const inc={
"SherlockEngine/App/AppBase.cpp":"App/AppBase.h Core/Log.h Core/Paths.h Core/Profiler.h",
"SherlockEngine/App/ContentBrowser.cpp":"App/ContentBrowser.h Core/Paths.h Core/Log.h Graphics/TextureLoader.h RHI/Device.h",
"SherlockEngine/App/DebugUI.cpp":"App/DebugUI.h Scene/Scene.h Scene/Camera.h Graphics/Renderer.h RHI/Device.h Graphics/Model.h Core/Engine.h Core/Log.h Core/Profiler.h RHI/RHI.h App/ContentBrowser.h",
"SherlockEngine/App/Editor.cpp":"App/Editor.h App/DebugUI.h App/ScriptCreator.h App/GameBuilder.h App/ProjectLauncher.h Core/Engine.h Core/Log.h Core/Profiler.h Core/Paths.h Core/AssetManager.h Graphics/Mesh.h Graphics/Model.h Scene/CameraComponent.h RHI/Device.h",
"SherlockEngine/App/EditorApp.cpp":"App/EditorApp.h App/DebugUI.h Graphics/Mesh.h Graphics/Model.h Core/Paths.h Core/Log.h Core/Profiler.h Scene/SceneSerializer.h Scene/CameraComponent.h Game/Components/Rigidbody.h App/ScriptCreator.h App/GameBuilder.h App/ProjectGenerator.h App/ProjectLauncher.h",
"SherlockEngine/App/EntryPoints.cpp":"App/EntryPoints.h App/EditorApp.h App/GameApp.h Core/Log.h",
"SherlockEngine/App/GameApp.cpp":"App/GameApp.h Core/Log.h Core/Paths.h Scene/SceneSerializer.h Scene/CameraComponent.h",
"SherlockEngine/App/GameBuilder.cpp":"App/GameBuilder.h App/ProjectGenerator.h Core/ScriptModule.h Core/Paths.h Core/Log.h",
"SherlockEngine/App/ProjectGenerator.cpp":"App/ProjectGenerator.h Core/Project.h Core/Paths.h Core/Log.h",
"SherlockEngine/App/ProjectLauncher.cpp":"App/ProjectLauncher.h App/ProjectGenerator.h Core/Log.h",
"SherlockEngine/App/ScriptCreator.cpp":"App/ScriptCreator.h Core/Paths.h Core/Log.h",
"SherlockEngine/App/AppBase.h":"Core/Engine.h Core/Config.h Core/Project.h Core/ScriptModule.h",
"SherlockEngine/App/ContentBrowser.h":"RHI/Handle.h",
"SherlockEngine/App/Editor.h":"App/ContentBrowser.h App/GameBuilder.h App/AppBase.h Scene/Camera.h",
"SherlockEngine/App/EditorApp.h":"App/AppBase.h Graphics/Model.h App/Editor.h",
"SherlockEngine/App/EntryPoints.h":"Core/EngineApi.h",
"SherlockEngine/App/GameApp.h":"App/AppBase.h",
"SherlockEditor/EditorMain.cpp":"App/EntryPoints.h",
"Projects/Sample/Scripts/Test.h":"Scene/Script.h"};
const nodes=[],edges=[];
const E=(s,t,type,w)=>edges.push({source:s,target:t,type,direction:'forward',weight:w});
const nm=p=>p.split('/').pop();
const F=(p,summary,tags,cx,notes)=>{const n={id:'file:'+p,type:'file',name:nm(p),filePath:p,summary,tags,complexity:cx};if(notes)n.languageNotes=notes;nodes.push(n);};
const fn=(p,name,summary,tags,cx,kind='function')=>{const r=by[p];const arr=kind==='class'?r.classes:r.functions;const o=arr.find(x=>x.name===name);if(!o)throw new Error(p+name);
 nodes.push({id:`${kind}:${p}:${name}`,type:kind,name,filePath:p,lineRange:[o.startLine,o.endLine],summary,tags,complexity:cx});E('file:'+p,`${kind}:${p}:${name}`,'contains',1.0);};
const A='SherlockEngine/App/';
F('Projects/Sample/Scripts/Test.h','샘플 프로젝트의 Test 스크립트 클래스 선언. 구현은 Test.cpp에 있으며 다른 스크립트가 GetBehaviour<Test>()로 사용할 수 있다.',['스크립트','헤더','샘플','리플렉션'],'simple');
fn('Projects/Sample/Scripts/Test.h','Test','Script를 상속하는 샘플 게임 스크립트로 Reflect/Start/Update를 선언한다.',['스크립트','샘플','컴포넌트'],'simple','class');
F('SherlockEditor/EditorMain.cpp','에디터 실행 파일의 wWinMain 진입점. EntryPoints의 RunEditor로 위임한다.',['진입점','에디터','윈도우'],'simple');
fn('SherlockEditor/EditorMain.cpp','wWinMain','Win32 진입 함수로 엔진 DLL의 에디터 실행을 호출한다.',['진입점','윈도우'],'simple');
F('SherlockEditor/SherlockEditor.vcxproj','SherlockEditor 실행 파일의 MSBuild 프로젝트 파일. EditorMain.cpp와 SherlockEngine 프로젝트 참조를 정의한다.',['빌드','msbuild','에디터','설정'],'moderate');
nodes[nodes.length-1].type='config';nodes[nodes.length-1].id='config:SherlockEditor/SherlockEditor.vcxproj';
F('SherlockEditor/SherlockEditor.vcxproj.filters','SherlockEditor 프로젝트의 Visual Studio 솔루션 탐색기 필터 정의.',['빌드','msbuild','설정'],'simple');
nodes[nodes.length-1].type='config';nodes[nodes.length-1].id='config:SherlockEditor/SherlockEditor.vcxproj.filters';
E('config:SherlockEditor/SherlockEditor.vcxproj','file:SherlockEditor/EditorMain.cpp','configures',0.6);
E('config:SherlockEditor/SherlockEditor.vcxproj','file:'+A+'EntryPoints.h','configures',0.6);
E('config:SherlockEditor/SherlockEditor.vcxproj.filters','config:SherlockEditor/SherlockEditor.vcxproj','related',0.5);
F(A+'AppBase.h','Win32 창, 엔진, 설정, 프로젝트 스크립트 DLL 로드를 담당하는 앱 기반 클래스 선언. 에디터와 게임 앱이 상속한다.',['앱','기반클래스','헤더','창관리'],'moderate');
fn(A+'AppBase.h','AppBase','Initialize/Run/MsgProc 루프와 가상 훅(OnInitialize, OnUpdate, OnGUI 등)을 제공하는 앱 추상 기반.',['앱','기반클래스','수명주기'],'moderate','class');
F(A+'AppBase.cpp','창 생성, 설정/로거 초기화, 프로젝트 해석과 스크립트 DLL 로드, 메인 루프, 윈도우 메시지 처리를 구현한다.',['앱','메인루프','윈도우','스크립트'],'complex');
for(const [n,s,t] of [['Initialize','설정, 로거, 창, 엔진, GUI를 순서대로 초기화한다.',['초기화']],['Run','메시지 펌프와 고정/가변 업데이트, 렌더링을 도는 메인 루프.',['메인루프']],['MsgProc','윈도우 메시지를 처리하고 리사이즈 및 포커스 이벤트를 앱에 전달한다.',['윈도우','이벤트']],['LoadProjectScripts','프로젝트 스크립트 DLL을 로드해 등록한다.',['스크립트','핫리로드']],['OpenProject','프로젝트를 열고 스크립트를 다시 로드한다.',['프로젝트']]])fn(A+'AppBase.cpp',n,s,t,'moderate');
fn(A+'AppBase.cpp','InitGUI','ImGui를 초기화하고 D3D11 및 Win32 백엔드를 연결한다.',['imgui','초기화'],'simple');
F(A+'ContentBrowser.h','에디터 콘텐츠 브라우저 패널과 드래그 페이로드(AssetDragPayload) 선언.',['에디터','에셋','헤더','ui'],'moderate');
fn(A+'ContentBrowser.h','ContentBrowser','폴더 트리, 타일 그리드, 썸네일 캐시, 에셋 드래그 소스를 가진 콘텐츠 브라우저 클래스.',['에디터','에셋','ui'],'complex','class');
fn(A+'ContentBrowser.h','AssetDragPayload','에셋을 씬 뷰로 드래그할 때 전달하는 페이로드 구조체.',['드래그앤드롭','에셋'],'simple','class');
F(A+'ContentBrowser.cpp','디렉터리 열거, 브레드크럼, 타일 그리드, 비동기 썸네일 로딩과 퇴출, 드래그 소스를 구현한 ImGui 콘텐츠 브라우저.',['에디터','에셋','imgui','썸네일'],'complex');
for(const [n,s,t,c] of [['Classify','확장자로 에셋 종류를 분류한다.',['에셋','분류'],'simple'],['DrawTypeIcon','에셋 종류별 아이콘을 ImDrawList로 직접 그린다.',['ui','아이콘'],'moderate'],['EnumerateDirectory','현재 디렉터리의 항목을 열거해 정렬한다.',['파일시스템'],'moderate'],['Draw','툴바, 폴더 트리, 타일 그리드를 배치하는 최상위 그리기 함수.',['ui'],'moderate'],['DrawTile','타일 하나의 썸네일, 라벨, 선택, 드래그를 처리한다.',['ui','타일'],'moderate'],['BeginDragSource','에셋 드래그 시작과 페이로드 설정을 처리한다.',['드래그앤드롭'],'moderate'],['PumpThumbnailLoads','프레임당 제한된 수의 썸네일 텍스처를 로드한다.',['썸네일','비동기'],'moderate'],['EvictThumbnails','오래된 썸네일을 퇴출해 메모리를 제한한다.',['썸네일','캐시'],'simple']])fn(A+'ContentBrowser.cpp',n,s,t,c);
F(A+'DebugUI.h','디버그 패널 그리기 함수들의 전방 선언 헤더.',['디버그','ui','헤더'],'simple');
F(A+'DebugUI.cpp','카메라, 렌더 설정, 그림자, 환경, 라이트, 머티리얼, 모델, 엔진, 프로파일러 패널과 로그 콘솔을 ImGui로 그린다.',['디버그','imgui','패널','프로파일러'],'complex');
for(const [n,s,t,c] of [['DrawRenderSettingsPanel','렌더 옵션을 조절하는 패널.',['렌더링','ui'],'moderate'],['DrawEnvironmentPanel','HDR 환경 맵과 IBL 설정을 편집한다.',['환경','ibl'],'moderate'],['DrawLightPanel','라이트 종류와 속성을 편집하는 패널.',['라이트','ui'],'complex'],['DrawMaterialPanel','PBR 머티리얼 파라미터를 편집한다.',['머티리얼','pbr'],'moderate'],['DrawProfilerPanel','프로파일러 측정값을 표시한다.',['프로파일러'],'moderate'],['DrawLogConsole','로그 메시지를 필터링해 보여주는 콘솔.',['로그','ui'],'moderate']])fn(A+'DebugUI.cpp',n,s,t,c);
F(A+'Editor.h','씬 뷰, 하이어라키, 인스펙터, 기즈모, 프로젝트 런처를 가진 에디터 UI 클래스 선언.',['에디터','ui','헤더'],'complex');
fn(A+'Editor.h','Editor','에디터 UI 전체를 관장하며 선택, 피킹, 에셋 드롭, 프로젝트 전환을 제공하는 클래스.',['에디터','ui','선택'],'complex','class');
F(A+'Editor.cpp','에디터의 메뉴바, 씬 뷰, 기즈모, 피킹, 하이어라키, 인스펙터, 에셋 드롭, 카메라 미리보기, 런처와 빌드 팝업을 구현한다.',['에디터','imgui','기즈모','피킹'],'complex');
for(const [n,s,t,c] of [['RayAabb','레이와 AABB의 교차 거리를 계산한다.',['수학','피킹'],'simple'],['Draw','에디터 UI 한 프레임을 그리는 진입 함수.',['ui'],'moderate'],['DrawSceneView','렌더 결과를 이미지로 표시하고 입력과 기즈모를 처리한다.',['씬뷰'],'complex'],['DrawGizmo','선택 오브젝트의 변환 기즈모를 그린다.',['기즈모'],'moderate'],['Pick','마우스 위치로 오브젝트를 레이캐스트 선택한다.',['피킹'],'moderate'],['HandleAssetDrop','씬 뷰에 드롭된 에셋을 종류별로 처리한다.',['드래그앤드롭'],'moderate'],['DrawHierarchy','씬 오브젝트 트리를 표시한다.',['하이어라키'],'moderate'],['DrawInspector','선택 오브젝트 컴포넌트를 편집한다.',['인스펙터'],'complex'],['DrawComponents','컴포넌트별 속성 편집 UI를 그린다.',['컴포넌트','리플렉션'],'complex'],['DrawBuildPopup','게임 빌드 옵션 팝업을 그리고 GameBuilder를 호출한다.',['빌드'],'moderate'],['DrawLauncher','최근 프로젝트 목록과 새 프로젝트 생성 런처 UI.',['프로젝트'],'moderate']])fn(A+'Editor.cpp',n,s,t,c);
fn(A+'Editor.cpp','ImGuiPropertyVisitor','리플렉션 속성 방문자로 값을 ImGui 위젯으로 편집한다.',['리플렉션','imgui'],'simple','class');
F(A+'EditorApp.h','AppBase를 상속한 에디터 애플리케이션 클래스 선언.',['에디터','앱','헤더'],'moderate');
fn(A+'EditorApp.h','EditorApp','씬 편집, 플레이 모드, 스크립트 재빌드, 자동화를 담당하는 앱 클래스.',['에디터','앱','플레이모드'],'complex','class');
F(A+'EditorApp.cpp','에디터 앱 구현: 초기화, 데모 씬 구성, 업데이트, 카메라 조작, 플레이/정지, 스크립트 빌드와 핫리로드, 씬 저장/로드, 자동화 실행.',['에디터','앱','핫리로드','자동화'],'complex');
for(const [n,s,t,c] of [['OnInitialize','렌더러, 에디터 UI, 프로젝트와 기본 씬을 초기화한다.',['초기화'],'moderate'],['BuildDemoScene','데모용 씬 오브젝트를 구성한다.',['씬'],'complex'],['OnUpdate','입력, 카메라, 시뮬레이션을 프레임마다 갱신한다.',['업데이트'],'moderate'],['BuildScripts','프로젝트 스크립트 DLL을 MSBuild로 빌드한다.',['스크립트','빌드'],'simple'],['ReloadScripts','스크립트 DLL을 언로드하고 다시 로드한다.',['핫리로드'],'moderate'],['StartPlay','씬을 스냅샷하고 플레이 모드로 전환한다.',['플레이모드'],'simple'],['StopPlay','스냅샷을 복원하며 편집 모드로 돌아간다.',['플레이모드'],'moderate'],['UpdateCamera','자유 시점 카메라 이동을 처리한다.',['카메라','입력'],'complex'],['RunAutomation','명령줄 자동화 스크립트를 실행한다.',['자동화','테스트'],'complex'],['PlaceModel','모델을 씬에 배치한다.',['씬','모델'],'moderate']])fn(A+'EditorApp.cpp',n,s,t,c);
F(A+'EntryPoints.h','엔진 DLL이 내보내는 RunEditor/RunGame 진입점 선언.',['진입점','헤더','dll'],'simple');
F(A+'EntryPoints.cpp','RunEditor와 RunGame 구현. 해당 앱 객체를 만들어 실행한다.',['진입점','dll','앱'],'simple');
fn(A+'EntryPoints.cpp','RunEditor','EditorApp을 생성해 초기화하고 실행한다.',['진입점','에디터'],'simple');
fn(A+'EntryPoints.cpp','RunGame','GameApp을 생성해 초기화하고 실행한다.',['진입점','게임'],'simple');
F(A+'GameApp.h','빌드된 게임을 실행하는 GameApp 클래스 선언. GUI를 사용하지 않는다.',['게임','앱','헤더'],'simple');
fn(A+'GameApp.h','GameApp','GUI 없이 씬을 로드해 실행하는 AppBase 파생 클래스.',['게임','앱'],'simple','class');
F(A+'GameApp.cpp','게임 모드 초기화(씬 로드, 카메라 탐색)와 업데이트를 구현한다.',['게임','앱','런타임'],'moderate');
fn(A+'GameApp.cpp','OnInitialize','시작 씬을 로드하고 메인 카메라를 찾는다.',['초기화','씬'],'moderate');
fn(A+'GameApp.cpp','OnUpdate','게임 프레임을 갱신한다.',['업데이트'],'moderate');
F(A+'GameBuilder.h','게임 빌드 옵션(Options)과 결과(Result) 구조체 및 빌더 API 선언.',['빌드','헤더','배포'],'simple');
fn(A+'GameBuilder.h','Options','빌드 출력 위치, 포함 씬 등 옵션 구조체.',['빌드','옵션'],'simple','class');
fn(A+'GameBuilder.h','Result','빌드 성공 여부와 메시지를 담는 구조체.',['빌드','결과'],'simple','class');
F(A+'GameBuilder.cpp','게임 배포 빌더: 씬이 참조하는 에셋을 수집하고 DLL, 스크립트, 에셋을 출력 폴더로 복사한다.',['빌드','배포','파일시스템'],'complex');
fn(A+'GameBuilder.cpp','CollectReferences','씬 파일에서 참조된 에셋 경로를 수집한다.',['에셋','의존성'],'moderate');
fn(A+'GameBuilder.cpp','CopyTree','디렉터리 트리를 재귀 복사한다.',['파일시스템'],'simple');
fn(A+'GameBuilder.cpp','Build','선택한 씬과 에셋을 모아 게임 패키지를 생성한다.',['빌드','배포'],'complex');
F(A+'ProjectGenerator.h','프로젝트 파일 생성과 MSBuild 실행 API 선언.',['프로젝트','생성기','헤더'],'simple');
F(A+'ProjectGenerator.cpp','스크립트용 vcxproj, filters, 솔루션 파일을 생성하고 MSBuild를 실행한다.',['프로젝트','생성기','msbuild','코드생성'],'complex');
fn(A+'ProjectGenerator.cpp','ScriptsProject','스크립트 DLL용 vcxproj 텍스트를 생성한다.',['코드생성','msbuild'],'complex');
fn(A+'ProjectGenerator.cpp','Generate','프로젝트에 필요한 빌드 파일 일체를 생성한다.',['프로젝트'],'moderate');
fn(A+'ProjectGenerator.cpp','EnsureUpToDate','생성 파일이 최신이 아니면 다시 만든다.',['프로젝트'],'simple');
fn(A+'ProjectGenerator.cpp','RunMsBuild','MSBuild를 외부 프로세스로 실행한다.',['msbuild','프로세스'],'moderate');
F(A+'ProjectLauncher.h','최근 프로젝트 목록 항목(Recent)과 런처 API 선언.',['프로젝트','런처','헤더'],'simple');
F(A+'ProjectLauncher.cpp','최근 프로젝트 저장/불러오기와 Win32 파일 및 폴더 선택 대화상자를 구현한다.',['프로젝트','런처','설정','대화상자'],'moderate');
fn(A+'ProjectLauncher.cpp','BrowseForFolder','폴더 선택 대화상자를 연다.',['win32','대화상자'],'moderate');
fn(A+'ProjectLauncher.cpp','AddRecent','최근 프로젝트 목록에 항목을 추가한다.',['최근목록'],'simple');
F(A+'ScriptCreator.cpp','새 스크립트 클래스의 헤더와 소스 템플릿을 생성하고 편집기로 연다.',['스크립트','코드생성','템플릿'],'moderate');
fn(A+'ScriptCreator.cpp','SourceTemplate','스크립트 .cpp 템플릿 문자열을 만든다.',['템플릿'],'simple');
fn(A+'ScriptCreator.cpp','HeaderTemplate','스크립트 .h 템플릿 문자열을 만든다.',['템플릿'],'simple');
fn(A+'ScriptCreator.cpp','Create','유효성을 검사한 뒤 스크립트 파일 쌍을 만든다.',['스크립트'],'simple');
for(const [p,l] of Object.entries(inc))for(const i of l.split(' '))E('file:'+p,'file:SherlockEngine/'+i,'imports',0.7);
for(const f of R){if(f.path.endsWith('.cpp')&&f.path.startsWith(A)){const h=f.path.replace('.cpp','.h');if(by[h])E('file:'+f.path,'file:'+h,'imports',0.7);}}
E('file:SherlockEditor/EditorMain.cpp','function:'+A+'EntryPoints.cpp:RunEditor','calls',0.8);
E('function:'+A+'EntryPoints.cpp:RunEditor','class:'+A+'EditorApp.h:EditorApp','calls',0.8);
E('function:'+A+'EntryPoints.cpp:RunGame','class:'+A+'GameApp.h:GameApp','calls',0.8);
E('class:'+A+'EditorApp.h:EditorApp','class:'+A+'AppBase.h:AppBase','inherits',0.9);
E('class:'+A+'GameApp.h:GameApp','class:'+A+'AppBase.h:AppBase','inherits',0.9);
E('file:Projects/Sample/Scripts/Test.h','file:Projects/Sample/Scripts/Test.cpp','related',0.5);
for(const e of ['Editor.h:Editor','ContentBrowser.h:ContentBrowser','AppBase.h:AppBase','EditorApp.h:EditorApp','GameApp.h:GameApp','GameBuilder.h:Options','GameBuilder.h:Result','ContentBrowser.h:AssetDragPayload']){const [f,n]=e.split(':');E('file:'+A+f,'class:'+A+f+':'+n,'exports',0.8);}
const seen=new Set();const out=edges.filter(e=>{const k=e.source+'|'+e.target+'|'+e.type;if(seen.has(k)||e.source===e.target)return false;seen.add(k);return true;});
const ids=new Set(nodes.map(n=>n.id));if(ids.size!==nodes.length)throw new Error('dup node');
const mk=(arr,i)=>arr;
const files=[...new Set(nodes.map(n=>n.filePath))].sort();
const parts=Math.ceil(Math.max(nodes.length/60,out.length/120));
const per=Math.ceil(files.length/parts);
for(let k=0;k<parts;k++){const fs2=new Set(files.slice(k*per,(k+1)*per));const pn=nodes.filter(n=>fs2.has(n.filePath));const pid=new Set(pn.map(n=>n.id));const pe=out.filter(e=>pid.has(e.source));
 fs.writeFileSync(`.ua/intermediate/batch-6-part-${k+1}.json`,JSON.stringify({nodes:pn,edges:pe},null,1));console.log('part',k+1,pn.length,pe.length);}
console.log(nodes.length,out.length);
