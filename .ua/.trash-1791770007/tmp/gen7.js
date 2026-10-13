const fs=require('fs');const R='C:/Users/User/Desktop/SherlockEngine/';
const ex=p=>fs.existsSync(R+p);
const nodes=[],edges=[];
const S='SherlockEngine/';
const files={
'App/ScriptCreator.h':['file','스크립트 프로젝트 생성 기능을 선언하는 에디터 앱용 헤더입니다.',['헤더','스크립트','에디터'],'simple'],
'Assets/Config/engine.ini':['config','엔진 실행 설정(그래픽·경로 등)을 키=값 형식으로 담은 INI 설정 파일로, Config 클래스가 읽어들입니다.',['설정','ini','런타임-설정'],'simple'],
'Assets/Models/Sponza/Sponza.bin':['file','Sponza glTF 모델의 정점·인덱스 버퍼를 담은 바이너리 데이터입니다.',['에셋','모델','바이너리','gltf'],'complex'],
'Assets/Models/Sponza/Sponza.gltf':['file','Sponza 장면의 메시·머티리얼·노드 구조를 기술한 glTF 모델 정의 파일입니다.',['에셋','모델','gltf','장면'],'complex'],
'Core/AssetManager.cpp':['file','모델 에셋 캐시의 로드 여부 확인, 언로드, 전체 비우기, 경로 목록 조회를 구현합니다.',['에셋-관리','캐시','구현'],'moderate'],
'Core/AssetManager.h':['file','경로 기반으로 모델을 캐싱하는 AssetManager 클래스를 선언합니다.',['에셋-관리','캐시','헤더'],'simple'],
'Core/Config.cpp':['file','INI 파일 파싱과 문자열·정수·실수·불리언 값 조회를 구현합니다.',['설정','파서','구현'],'moderate'],
'Core/Config.h':['file','키-값 설정 저장소 Config 클래스를 선언합니다.',['설정','헤더','유틸리티'],'simple'],
'Core/Engine.cpp':['file','엔진의 초기화·종료, 프레임 시작/렌더/종료, 리사이즈 처리를 구현하며 서브시스템을 조율합니다.',['엔진-코어','프레임-루프','진입점'],'moderate'],
'Core/Engine.h':['file','RHI, 렌더러, 씬, 입력, 에셋 매니저 등을 소유하는 Engine 클래스를 선언합니다.',['엔진-코어','헤더','서비스'],'moderate'],
'Core/EngineApi.h':['file','엔진 DLL 내보내기/가져오기를 위한 SHERLOCK_API 매크로를 정의합니다.',['dll','매크로','헤더'],'simple'],
'Core/GameTimer.cpp':['file','고해상도 타이머 기반의 총 시간·델타 시간 계산과 시작/정지/리셋을 구현합니다.',['타이머','시간','구현'],'moderate'],
'Core/GameTimer.h':['file','프레임 타이밍을 측정하는 GameTimer 클래스를 선언합니다.',['타이머','헤더','시간'],'simple'],
'Core/Input.cpp':['file','키보드·마우스 이벤트 수신과 프레임 단위 눌림/뗌 상태 판별을 구현합니다.',['입력','이벤트-핸들러','구현'],'moderate'],
'Core/Input.h':['file','키·마우스 상태를 추적하는 Input 인터페이스를 선언합니다.',['입력','헤더','상태'],'simple'],
'Core/Log.cpp':['file','콘솔·디버거·파일 출력과 로그 히스토리, HRESULT 문자열 변환 등 로깅 시스템을 구현합니다.',['로깅','진단','구현'],'complex'],
'Core/Log.h':['file','로그 레벨, Entry, InitDesc와 로깅 API를 선언합니다.',['로깅','헤더','진단'],'moderate'],
'Core/Paths.cpp':['file','실행 파일 위치 기준 엔진·프로젝트 루트, 셰이더·에셋·씬 경로 해석을 구현합니다.',['경로','유틸리티','파일시스템'],'complex'],
'Core/Paths.h':['file','엔진·프로젝트 경로 조회 함수들을 선언합니다.',['경로','헤더','유틸리티'],'simple'],
'Core/Profiler.cpp':['file','CPU 스코프와 GPU 타임스탬프 쿼리 기반 프레임 프로파일러를 구현합니다.',['프로파일링','성능','gpu'],'moderate'],
'Core/Profiler.h':['file','CPU/GPU 샘플 구조체와 RAII 방식의 ProfileScope, GpuProfileScope를 선언합니다.',['프로파일링','raii','헤더'],'moderate'],
'Core/Project.cpp':['file','프로젝트 파일 탐색, 로드/저장/생성과 솔루션·스크립트 프로젝트 경로 계산을 구현합니다.',['프로젝트','파일-입출력','구현'],'moderate'],
'Core/Project.h':['file','프로젝트 메타데이터와 디렉터리 조회를 제공하는 Project 클래스를 선언합니다.',['프로젝트','헤더','메타데이터'],'simple'],
'Core/ScriptModule.cpp':['file','스크립트 DLL을 복사본으로 로드/언로드하고 새 빌드 감지로 핫리로드를 지원합니다.',['스크립트','핫리로드','dll'],'moderate'],
'Core/ScriptModule.h':['file','프로젝트 스크립트 DLL을 관리하는 ScriptModule 클래스를 선언합니다.',['스크립트','핫리로드','헤더'],'simple'],
};
for(const[p,[t,s,tags,c]] of Object.entries(files)){
 const fp=S+p;
 nodes.push({id:t+':'+fp,type:t,name:p.split('/').pop(),filePath:fp,summary:s,tags,complexity:c});
}
const cls=[
['Core/AssetManager.h','AssetManager',17,55,'모델을 정규화된 경로로 캐싱하고 언로드·정리·목록 조회를 제공하는 클래스입니다.',['에셋-관리','캐시']],
['Core/Config.h','Config',13,33,'INI에서 읽은 키-값을 보관하고 타입별 getter를 제공하는 설정 클래스입니다.',['설정','유틸리티']],
['Core/Engine.h','Engine',20,70,'RHI·렌더러·씬·입력·에셋을 묶어 프레임 수명주기를 관리하는 엔진 최상위 클래스입니다.',['엔진-코어','서비스']],
['Core/Project.h','Project',16,46,'프로젝트 파일 로드/저장/생성과 에셋·스크립트·바이너리 디렉터리를 제공하는 클래스입니다.',['프로젝트','메타데이터']],
['Core/ScriptModule.h','ScriptModule',20,55,'스크립트 DLL의 로드, 언로드, 새 빌드 감지를 담당하는 핫리로드 클래스입니다.',['스크립트','핫리로드']],
['Core/Profiler.h','ProfileScope',54,61,'생성/소멸 시 CPU 구간을 자동 기록하는 RAII 스코프입니다.',['프로파일링','raii']],
['Core/Profiler.h','GpuProfileScope',63,72,'커맨드 리스트에 GPU 타임스탬프 구간을 자동 기록하는 RAII 스코프입니다.',['프로파일링','raii','gpu']],
];
for(const[p,n,a,b,s,t] of cls){const fp=S+p;nodes.push({id:`class:${fp}:${n}`,type:'class',name:n,filePath:fp,lineRange:[a,b],summary:s,tags:t,complexity:'moderate'});
 edges.push({source:'file:'+fp,target:`class:${fp}:${n}`,type:'contains',direction:'forward',weight:1.0});
 edges.push({source:'file:'+fp,target:`class:${fp}:${n}`,type:'exports',direction:'forward',weight:0.8});}
const fn=[
['Core/Engine.cpp','Initialize',21,69,'엔진 서브시스템(장치, 렌더러, 씬 등)을 순서대로 초기화합니다.',['초기화','엔진-코어']],
['Core/Engine.cpp','Render',95,129,'한 프레임의 씬 렌더링 패스를 기록하고 실행합니다.',['렌더링','엔진-코어']],
['Core/ScriptModule.cpp','Load',70,123,'스크립트 DLL을 복사본으로 로드하고 진입 함수를 연결합니다.',['스크립트','핫리로드']],
['Core/Paths.cpp','GetShaderPath',141,165,'빌드 출력과 소스 트리를 고려해 셰이더 파일 경로를 해석합니다.',['경로','셰이더']],
['Core/Log.cpp','Emit',87,118,'로그 한 줄을 포맷해 콘솔·디버거·히스토리로 내보냅니다.',['로깅','출력']],
['Core/Profiler.cpp','BeginFrame',59,108,'프레임 시작 시 이전 프레임의 CPU/GPU 측정값을 정리하고 새 프레임을 준비합니다.',['프로파일링','프레임']],
];
for(const[p,n,a,b,s,t] of fn){const fp=S+p;nodes.push({id:`function:${fp}:${n}`,type:'function',name:n,filePath:fp,lineRange:[a,b],summary:s,tags:t,complexity:'moderate'});
 edges.push({source:'file:'+fp,target:`function:${fp}:${n}`,type:'contains',direction:'forward',weight:1.0});}
for(const p of Object.keys(files)){ if(!/\.(cpp|h)$/.test(p))continue;
 const src=fs.readFileSync(R+S+p,'utf8');
 for(const m of src.matchAll(/#include\s+"([^"]+)"/g)){const t=S+m[1];if(m[1]==='pch.h')continue;
  if(ex(t)&&t!==S+p){edges.push({source:'file:'+S+p,target:'file:'+t,type:'imports',direction:'forward',weight:0.7});}
  else console.log('unresolved',p,m[1]);}}
edges.push({source:'config:'+S+'Assets/Config/engine.ini',target:'file:'+S+'Core/Config.cpp',type:'configures',direction:'forward',weight:0.6});
edges.push({source:'file:'+S+'Assets/Models/Sponza/Sponza.gltf',target:'file:'+S+'Assets/Models/Sponza/Sponza.bin',type:'depends_on',direction:'forward',weight:0.6});
fs.writeFileSync(R+'.ua/intermediate/batch-7.json',JSON.stringify({nodes,edges},null,1));
console.log(nodes.length,edges.length);
