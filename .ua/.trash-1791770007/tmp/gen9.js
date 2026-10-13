const fs=require('fs');
const P='SherlockEngine/RHI/';
const files={
'D3D11/D3D11Device.h':['D3D11 백엔드의 Device 구현 클래스 선언. 스왑체인, 리소스 풀, 파이프라인 캐시, 커맨드 리스트를 소유한다.',['rhi','d3d11','device','header'],'moderate'],
'D3D11/D3D11Resources.h':['D3D11 버퍼·텍스처·셰이더·샘플러·바인딩 레이아웃·리소스 셋의 백엔드 리소스 구조체 정의와 포맷 변환 헬퍼.',['rhi','d3d11','resource','header'],'moderate'],
'D3D11/PipelineState.cpp':['D3D11 PipelineState의 Create(래스터라이저/블렌드/깊이 스텐실/입력 레이아웃 객체 생성)와 Bind(파이프라인 상태 일괄 적용) 구현.',['rhi','d3d11','pipeline-state','implementation'],'moderate'],
'D3D11/PipelineState.h':['D3D11 파이프라인 상태 객체를 묶는 PipelineState 클래스 선언.',['rhi','d3d11','pipeline-state','header'],'simple'],
'D3D11/PipelineStateCache.cpp':['PipelineStateDesc 해시를 키로 D3D11 PipelineState를 생성·재사용하는 캐시 구현.',['rhi','d3d11','cache','implementation'],'simple'],
'D3D11/PipelineStateCache.h':['D3D11 PipelineState를 desc 키로 캐시하는 PipelineStateCache 클래스 선언.',['rhi','d3d11','cache','header'],'simple'],
'D3D12/D3D12CommandList.cpp':['D3D12 커맨드 리스트 구현. 렌더 패스 시작, 파이프라인/리소스 셋 바인딩, 드로우, 배리어, 버퍼 복사를 기록한다.',['rhi','d3d12','command-list','implementation'],'complex'],
'D3D12/D3D12CommandList.h':['RHI CommandList 인터페이스를 D3D12로 구현하는 D3D12CommandList 클래스 선언.',['rhi','d3d12','command-list','header'],'simple'],
'D3D12/D3D12Common.h':['D3D12 백엔드 공통 헤더. D3D12/DXGI 포함과 공용 정의를 모은다.',['rhi','d3d12','common','header'],'simple'],
'D3D12/D3D12Convert.cpp':['RHI 추상 열거형·디스크립터를 D3D12/DXGI 타입으로 변환하는 함수들과 힙 속성, 버퍼 desc, 전이 배리어 생성 헬퍼 구현.',['rhi','d3d12','conversion','utility'],'complex'],
'D3D12/D3D12Convert.h':['RHI 타입을 D3D12 타입으로 변환하는 ToD3D12/ToDXGI 계열 함수 선언.',['rhi','d3d12','conversion','header'],'simple'],
'D3D12/D3D12DescriptorHeap.h':['D3D12 디스크립터 힙 래퍼. 힙 초기화와 슬롯 할당을 관리하는 D3D12DescriptorHeap 클래스.',['rhi','d3d12','descriptor-heap','header'],'moderate'],
'D3D12/D3D12Device.cpp':['D3D12 Device 전체 구현. 디바이스/스왑체인 초기화, 프레임 리소스, 버퍼·텍스처·샘플러·셰이더·루트 시그니처·리소스 셋 생성, ImGui, 타임스탬프, 백버퍼 리드백을 담당하는 가장 큰 파일.',['rhi','d3d12','device','implementation','swapchain'],'complex'],
'D3D12/D3D12Device.h':['RHI Device를 구현하는 D3D12Device 클래스 선언. 리소스 풀, 디스크립터 힙, 프레임 리소스, 파이프라인 캐시를 소유한다.',['rhi','d3d12','device','header'],'complex'],
'D3D12/D3D12PipelineState.cpp':['D3D12 PSO 생성과 PipelineStateDesc 키 기반 GetOrCreate 캐시, Clear 구현.',['rhi','d3d12','pipeline-state','cache'],'moderate'],
'D3D12/D3D12PipelineState.h':['D3D12RootLayout, D3D12PipelineState, D3D12PipelineStateCache 선언.',['rhi','d3d12','pipeline-state','header'],'simple'],
'D3D12/D3D12Resources.h':['D3D12 버퍼·텍스처·셰이더·샘플러·바인딩 레이아웃·리소스 셋 백엔드 리소스 구조체 정의.',['rhi','d3d12','resource','header'],'simple'],
'DXBC/ShaderCompiler.cpp':['FXC(D3DCompile) 기반 셰이더 컴파일, 디스크 캐시 로드/컴파일, 상수 버퍼 크기 검증, 셰이더 리플렉션 바인딩 추출 구현.',['rhi','shader-compiler','dxbc','reflection'],'complex'],
'Device.h':['백엔드 중립 RHI Device 추상 인터페이스와 DeviceDesc. 리소스 생성, 프레임, 커맨드 리스트 접근을 정의한다.',['rhi','interface','device','abstraction'],'moderate'],
'GraphicsConfig.h':['그래픽스 공용 상수 설정 헤더.',['rhi','config','constants','header'],'simple'],
'Handle.h':['세대(generation) 기반 타입 안전 핸들 템플릿과 Shader/Pipeline/Buffer/Texture 등 태그 타입 별칭.',['rhi','handle','type-safety','header'],'simple'],
'PipelineTypes.cpp':['파이프라인 상태 desc의 해시 함수와 동등 비교 연산자 구현으로 PSO 캐시 키를 지원.',['rhi','pipeline-state','hashing','implementation'],'moderate'],
'PipelineTypes.h':['포맷, 래스터라이저/깊이 스텐실/블렌드/버텍스 레이아웃 desc와 PipelineStateDesc, 해셔 정의. PSO 설계의 핵심 타입.',['rhi','pipeline-state','type-definition','header'],'moderate'],
'RHI.cpp':['백엔드 선택에 따라 D3D11 또는 D3D12 Device를 생성하는 CreateDevice 팩토리 구현.',['rhi','factory','entry-point','implementation'],'simple'],
'RHI.h':['RHI 공개 진입 헤더. Device와 CommandList를 포함하고 CreateDevice를 노출한다.',['rhi','entry-point','barrel','header'],'simple'],
};
const fn={
'D3D12/D3D12Device.cpp':[['Init',39,54,'D3D12Device 초기화 진입점. 디바이스, 스왑체인, 프레임 리소스 초기화를 순서대로 호출한다.'],['InitDevice',66,158,'DXGI 어댑터 선택, 디버그 레이어, D3D12 디바이스와 큐 생성.'],['InitSwapChain',160,191,'플립 모델 스왑체인을 생성한다.'],['CreateTexture',630,777,'텍스처 리소스와 뷰를 생성하고 초기 데이터를 업로드한다.'],['GetOrCreateRootLayout',979,1103,'바인딩 레이아웃으로부터 루트 시그니처를 만들고 캐시한다.'],['CreateResourceSet',1157,1241,'리소스 셋의 디스크립터를 구성한다.'],['ReadTimestamps',1351,1374,'GPU 타임스탬프 쿼리 결과를 읽어 패스 시간을 계산한다.']],
'D3D12/D3D12CommandList.cpp':[['BeginRenderPass',36,109,'렌더 타깃/깊이 클리어와 배리어를 포함해 렌더 패스를 시작한다.'],['ApplyBindings',203,256,'리소스 셋의 디스크립터 테이블을 루트 시그니처에 바인딩한다.'],['Barrier',286,309,'리소스 상태 전이 배리어를 기록한다.']],
'DXBC/ShaderCompiler.cpp':[['CompileFromFile',29,99,'HLSL 파일을 D3DCompile로 컴파일하고 오류를 로그로 출력한다.'],['LoadOrCompile',128,171,'캐시된 바이트코드를 로드하고 없으면 컴파일한다.'],['ReflectBindings',204,242,'셰이더 리플렉션으로 바인딩 정보를 추출한다.']],
'D3D11/PipelineState.cpp':[['Create',7,104,'desc로부터 D3D11 상태 객체들을 생성한다.'],['Bind',106,119,'파이프라인 상태를 디바이스 컨텍스트에 적용한다.']],
'D3D12/D3D12PipelineState.cpp':[['Create',7,93,'D3D12 그래픽스 PSO를 생성한다.'],['GetOrCreate',97,123,'desc 해시로 PSO를 조회하거나 생성한다.']],
'D3D11/PipelineStateCache.cpp':[['GetOrCreate',6,38,'desc 키로 PipelineState를 조회/생성한다.']],
'PipelineTypes.cpp':[['Hash',122,173,'PipelineStateDesc 전체 해시를 계산한다.']],
'RHI.cpp':[['CreateDevice',22,52,'백엔드 종류에 맞는 Device 구현을 생성해 반환한다.']],
'D3D12/D3D12Convert.cpp':[['ToDXGI',7,27,'RHI Format을 DXGI_FORMAT으로 변환한다.'],['TransitionBarrier',286,296,'전이 배리어 구조체를 만든다.']],
};
const cls={
'D3D11/D3D11Device.h':[['D3D11Device',15,153,'D3D11 백엔드 Device 구현.']],
'D3D11/D3D11Resources.h':[['D3D11Texture',54,160,'D3D11 텍스처와 SRV/RTV/DSV 뷰를 보관.']],
'D3D11/PipelineState.h':[['PipelineState',18,36,'D3D11 파이프라인 상태 묶음.']],
'D3D11/PipelineStateCache.h':[['PipelineStateCache',18,32,'PipelineState 캐시.']],
'D3D12/D3D12CommandList.h':[['D3D12CommandList',17,66,'D3D12 커맨드 리스트 구현.']],
'D3D12/D3D12DescriptorHeap.h':[['D3D12DescriptorHeap',15,105,'디스크립터 힙 할당기.']],
'D3D12/D3D12Device.h':[['D3D12Device',25,227,'D3D12 백엔드 Device 구현.']],
'D3D12/D3D12PipelineState.h':[['D3D12RootLayout',18,32,'루트 시그니처 레이아웃.'],['D3D12PipelineState',34,50,'D3D12 PSO 래퍼.'],['D3D12PipelineStateCache',54,67,'D3D12 PSO 캐시.']],
'D3D12/D3D12Resources.h':[['D3D12Buffer',19,26,'D3D12 버퍼 리소스.'],['D3D12Texture',28,42,'D3D12 텍스처 리소스.']],
'Device.h':[['Device',40,114,'백엔드 중립 RHI Device 추상 인터페이스.'],['DeviceDesc',32,38,'Device 생성 옵션.']],
'PipelineTypes.h':[['PipelineStateDesc',193,219,'PSO 전체 상태를 기술하는 desc.'],['BlendDesc',168,173,'블렌드 상태 desc.'],['RasterizerDesc',118,134,'래스터라이저 desc.']],
};
const nodes=[],edges=[];
const e=(s,t,ty,w)=>edges.push({source:s,target:t,type:ty,direction:'forward',weight:w});
for(const [k,[s,t,c]] of Object.entries(files)){
 nodes.push({id:'file:'+P+k,type:'file',name:k.split('/').pop(),filePath:P+k,summary:s,tags:t,complexity:c});}
for(const [k,l] of Object.entries(fn))for(const [n,a,b,s] of l){const id='function:'+P+k+':'+n;
 nodes.push({id,type:'function',name:n,filePath:P+k,lineRange:[a,b],summary:s,tags:['rhi','function',k.includes('D3D12')?'d3d12':'rhi-core'],complexity:b-a>60?'moderate':'simple'});
 e('file:'+P+k,id,'contains',1);}
for(const [k,l] of Object.entries(cls))for(const [n,a,b,s] of l){const id='class:'+P+k+':'+n;
 nodes.push({id,type:'class',name:n,filePath:P+k,lineRange:[a,b],summary:s,tags:['rhi','class',k.includes('D3D12')?'d3d12':'rhi-core'],complexity:b-a>60?'moderate':'simple'});
 e('file:'+P+k,id,'contains',1);e('file:'+P+k,id,'exports',0.8);}
const seen=new Set();
for(const k of Object.keys(files)){
 const src='SherlockEngine/RHI/'+k;
 const txt=fs.readFileSync(src,'utf8');
 for(const m of txt.matchAll(/#include "([^"]+)"/g)){
  const inc=m[1];if(inc==='pch.h')continue;
  const tgt='SherlockEngine/'+inc;const key=src+'>'+tgt;
  if(seen.has(key)||src===tgt||!fs.existsSync(tgt))continue;
  seen.add(key);e('file:'+src,'file:'+tgt,'imports',0.7);}}
e('class:'+P+'D3D12/D3D12Device.h:D3D12Device','class:'+P+'Device.h:Device','implements',0.9);
e('class:'+P+'D3D11/D3D11Device.h:D3D11Device','class:'+P+'Device.h:Device','implements',0.9);
e('function:'+P+'RHI.cpp:CreateDevice','class:'+P+'D3D12/D3D12Device.h:D3D12Device','calls',0.8);
e('function:'+P+'RHI.cpp:CreateDevice','class:'+P+'D3D11/D3D11Device.h:D3D11Device','calls',0.8);
fs.writeFileSync('.ua/intermediate/batch-9.json',JSON.stringify({nodes,edges}));
console.log(nodes.length,edges.length);
