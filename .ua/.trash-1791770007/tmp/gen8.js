const fs=require('fs');
const P='SherlockEngine/';
const r=JSON.parse(fs.readFileSync('.ua/tmp/ua-file-extract-results-8.json','utf8'));
const by={};for(const f of r.results)by[f.path]=f;
const F={
'Core/Time.cpp':['Time 정적 클래스의 구현으로, 프레임 델타 타임 측정(Tick), 초기화(Reset), 고정 스텝 소비(ConsumeFixedStep)를 처리한다.',['timing','game-loop','fixed-timestep'],'simple'],
'Core/Time.h':['스크립트에서 쓰는 전역 시간 API(deltaTime, 고정 스텝 등)를 선언하며 GameTimer를 감싸 엔진 DLL로 내보낸다.',['timing','public-api','header'],'simple'],
'Game/Components/FollowTarget.cpp':['대상 오브젝트를 부드럽게 따라가는 Behaviour 컴포넌트로, Reflect로 에디터에 속성을 노출하고 Update에서 위치를 보간한다.',['component','behaviour','camera-follow','reflection'],'simple'],
'Game/Components/Rigidbody.cpp':['간단한 물리 컴포넌트 Rigidbody의 구현으로, FixedUpdate에서 속도와 중력을 적분해 위치를 갱신한다.',['component','physics','fixed-update','reflection'],'simple'],
'Game/Components/Rigidbody.h':['Behaviour를 상속하는 Rigidbody 컴포넌트 선언으로 속도·중력 등 물리 속성을 정의한다.',['component','physics','header'],'simple'],
'Graphics/Mesh.cpp':['구·큐브·평면·실린더 프리미티브 생성, 바운드 계산, 탄젠트 계산 등 CPU 메시 데이터 구축 로직을 구현한다.',['mesh','procedural-geometry','tangent','geometry'],'complex'],
'Graphics/Mesh.h':['Submesh, Bounds, Mesh 클래스를 선언하며 정점/인덱스 데이터와 프리미티브 팩토리, 바운딩 정보를 제공한다.',['mesh','data-model','header','geometry'],'moderate'],
'Graphics/Model.cpp':['glTF 파일을 읽어 메시, 머티리얼, 텍스처 이미지, 노드 변환을 엔진 형식으로 변환하는 로더 구현.',['gltf','model-loader','asset-import','pbr'],'complex'],
'Graphics/Model.h':['glTF 로드 결과를 담는 Model, ModelImage, ModelInstance, ModelStats 구조체와 로드 옵션을 선언한다.',['model','data-model','header','gltf'],'moderate'],
'Graphics/Renderer.cpp':['엔진의 핵심 렌더러 구현으로, RHI를 통해 초기화, 셰이더 로드/핫리로드, 그림자·메인·프리뷰·UI 패스, 드로우 리스트 구성, 텍스처/큐브맵 캐시, 환경(IBL) 처리를 수행한다.',['renderer','rendering-pipeline','shadow-pass','hot-reload','ibl'],'complex'],
'Graphics/Renderer.h':['Renderer 클래스와 RenderSettings를 선언하며 GPU 리소스 캐시, 패스 함수, 오프스크린 타깃, 파이프라인 조회 API를 정의한다.',['renderer','header','rendering-pipeline','public-api'],'complex'],
'Graphics/Screenshot.cpp':['픽셀 버퍼를 PNG 파일로 저장하는 SavePng 함수를 구현한다.',['screenshot','image-io','utility'],'simple'],
'Graphics/Screenshot.h':['PNG 스크린샷 저장 함수를 선언하는 작은 헤더.',['screenshot','header','utility'],'simple'],
'Graphics/ShaderConstants.h':['HLSL 상수 버퍼와 1:1로 대응하는 PerFrame, PerObject, Light, Material 상수 구조체와 레지스터 슬롯, 크기 검증 함수를 정의한다.',['constant-buffer','shader-interface','data-model','header'],'moderate'],
'Graphics/TextureLoader.cpp':['DirectXTex를 사용해 파일/메모리에서 텍스처를 로드하고, 체커·단색·큐브맵(HDR 포함) 및 썸네일 이미지를 생성한다.',['texture','loader','cubemap','directxtex'],'complex'],
'Graphics/TextureLoader.h':['TextureImage 구조체와 TextureLoader 함수들을 선언하며 CPU 측 텍스처 데이터를 RHI 리소스 설명자와 연결한다.',['texture','header','data-model'],'simple'],
'Graphics/VertexTypes.h':['VERTEX 구조체와 정점 포맷(PCNTT) 레이아웃 생성, 스트라이드 계산 헬퍼를 정의한다.',['vertex','type-definition','input-layout','header'],'simple'],
'RHI/BindingTypes.h':['바인딩 슬롯, 바인딩 레이아웃, 리소스 바인딩, 리소스 셋 설명자 등 API 독립적 바인딩 타입을 정의한다.',['rhi','binding','type-definition','header'],'simple'],
'RHI/CommandList.h':['렌더 패스, 파이프라인, 리소스 셋, 드로우, 배리어 등을 기록하는 API 독립적 CommandList 추상 인터페이스.',['rhi','abstract-interface','command-list','header'],'moderate'],
'RHI/D3D11/D3D11CommandList.cpp':['CommandList의 D3D11 구현으로, 렌더 패스 시작/종료, PSO 적용, 리소스 셋 바인딩, 드로우, 버퍼 복사, 타임스탬프 기록을 수행한다.',['rhi','d3d11','command-list','draw-calls'],'complex'],
'RHI/D3D11/D3D11CommandList.h':['D3D11CommandList 클래스를 선언하며 CommandList 인터페이스를 D3D11 디바이스 컨텍스트로 구현하기 위한 상태를 보관한다.',['rhi','d3d11','command-list','header'],'simple'],
'RHI/D3D11/D3D11Common.h':['D3D11 백엔드 전반에서 공유하는 D3D11/DXGI 헤더 등 공통 include 모음.',['rhi','d3d11','common-header','dependencies'],'simple'],
'RHI/D3D11/D3D11Convert.cpp':['RHI의 API 독립적 enum(포맷, 필터, 블렌드, 비교 함수 등)을 DXGI/D3D11 값으로 변환하는 함수들을 구현한다.',['rhi','d3d11','conversion','enum-mapping'],'moderate'],
'RHI/D3D11/D3D11Convert.h':['RHI 타입을 D3D11 타입으로 변환하는 ToDXGI/ToD3D11 오버로드 선언.',['rhi','d3d11','conversion','header'],'simple'],
'RHI/D3D11/D3D11Device.cpp':['Device의 D3D11 구현으로, 디바이스/스왑체인 초기화, 버퍼·텍스처·샘플러·셰이더·바인딩 레이아웃·리소스 셋 생성/파괴, ImGui 연동, GPU 타임스탬프와 리드백을 처리한다.',['rhi','d3d11','device','resource-management','swapchain'],'complex'],
};
const C='class';
const S={
'Core/Time.cpp':[['Tick','프레임 간 경과 시간을 계산해 deltaTime과 고정 스텝 누적기를 갱신한다.',['timing','game-loop']],['ConsumeFixedStep','누적된 시간에서 고정 스텝 하나를 소비할 수 있는지 반환한다.',['fixed-timestep','physics']]],
'Game/Components/FollowTarget.cpp':[['FollowTarget','대상 오브젝트를 따라가는 Behaviour 컴포넌트.',['component','camera-follow'],C]],
'Game/Components/Rigidbody.cpp':[['FixedUpdate','고정 스텝마다 중력과 속도를 적분해 위치를 갱신한다.',['physics','fixed-update']]],
'Graphics/Mesh.cpp':[['CreateSphere','구 프리미티브의 정점·인덱스를 생성한다.',['procedural-geometry','mesh']],['CreateCube','면별 법선을 가진 큐브 메시를 생성한다.',['procedural-geometry','mesh']],['CreatePlane','평면 메시를 생성한다.',['procedural-geometry','mesh']],['CreateCylinder','측면과 캡을 포함한 실린더 메시를 생성한다.',['procedural-geometry','mesh']],['ComputeTangents','UV 기반으로 정점별 탄젠트를 계산해 노멀맵을 지원한다.',['tangent','normal-mapping']],['Finalize','바운드와 서브메시 정보를 확정해 메시를 사용 가능 상태로 만든다.',['mesh','bounds']]],
'Graphics/Mesh.h':[['Mesh','CPU 측 정점/인덱스 데이터와 서브메시, 바운드를 보유하며 프리미티브 팩토리를 제공하는 클래스.',['mesh','data-model'],C],['Bounds','바운딩 박스 구조체로 중심과 크기를 제공한다.',['bounds','data-model'],C],['Submesh','인덱스 범위 단위의 서브메시 구조체.',['mesh','data-model'],C]],
'Graphics/Model.cpp':[['LoadGltf','glTF 파일을 파싱해 메시, 머티리얼, 이미지, 노드 인스턴스를 Model로 변환하는 진입 함수.',['gltf','model-loader']],['ConvertMesh','glTF 프리미티브를 엔진 Mesh로 변환하며 좌표계 보정을 처리한다.',['gltf','mesh-conversion']],['ConvertMaterial','glTF PBR 머티리얼을 엔진 Material로 변환한다.',['gltf','pbr','material']],['CollectImage','glTF 이미지를 수집해 ModelImage로 저장한다.',['gltf','texture']]],
'Graphics/Model.h':[['Model','로드된 glTF 모델의 메시, 머티리얼, 이미지, 인스턴스를 담는 클래스.',['model','data-model'],C]],
'Graphics/Renderer.cpp':[['Initialize','디바이스, 셰이더, 샘플러, 그림자 리소스, 내장 텍스처를 초기화한다.',['renderer','initialization']],['LoadShaders','HLSL 셰이더를 컴파일하고 바인딩 레이아웃과 파이프라인을 구성한다.',['shader','pipeline']],['ReloadShaders','셰이더를 다시 컴파일해 파이프라인을 무효화하는 핫리로드 처리.',['hot-reload','shader']],['Render','한 프레임의 그림자, 메인, 프리뷰 패스를 실행하는 최상위 렌더 함수.',['renderer','frame']],['BuildDrawList','씬을 순회해 드로우 항목을 구성한다.',['draw-list','culling']],['RenderShadowPass','방향광 시점의 섀도맵을 렌더링한다.',['shadow-pass','rendering']],['RenderMainPass','메인 색상 패스를 렌더링한다.',['main-pass','rendering']],['DrawItems','드로우 항목의 파이프라인, 리소스 셋, 상수 버퍼를 바인딩해 드로우한다.',['draw-calls','rendering']],['GetOrLoadTexture','경로별 텍스처를 캐시에서 찾거나 로드해 GPU 리소스로 만든다.',['texture-cache','resource']],['GetOrLoadCubemap','큐브맵을 캐시에서 찾거나 로드한다.',['cubemap','ibl','texture-cache']],['ResolveEnvironment','씬 환경 설정을 해석해 IBL 큐브맵 리소스를 확정한다.',['ibl','environment']],['SetSceneTarget','씬 오프스크린 렌더 타깃을 생성/재생성한다.',['offscreen','render-target']],['Shutdown','모든 GPU 리소스와 캐시를 해제한다.',['renderer','cleanup']]],
'Graphics/Renderer.h':[['Renderer','RHI 위에서 씬을 렌더링하는 중심 클래스로 패스, 캐시, 파이프라인, 오프스크린 타깃을 관리한다.',['renderer','rendering-pipeline'],C],['RenderSettings','그림자·환경 등 렌더러 옵션 구조체.',['settings','data-model'],C]],
'Graphics/Screenshot.cpp':[['SavePng','픽셀 버퍼를 PNG 파일로 인코딩해 저장한다.',['screenshot','image-io']]],
'Graphics/ShaderConstants.h':[['PerFrameConstants','프레임 단위 상수 버퍼 구조체.',['constant-buffer','data-model'],C],['PerObjectConstants','오브젝트별 상수 버퍼 구조체.',['constant-buffer','data-model'],C],['LightConstants','조명 데이터 상수 구조체.',['constant-buffer','light'],C],['MaterialConstants','PBR 머티리얼 파라미터 상수 구조체.',['constant-buffer','pbr','material'],C]],
'Graphics/TextureLoader.cpp':[['FinishImage','스크래치 이미지를 변환/밉맵 처리해 TextureImage로 마무리한다.',['texture','mipmap']],['LoadFromFile','파일에서 텍스처를 읽어 TextureImage를 만든다.',['texture','loader']],['LoadCubeFromFile','파일에서 큐브맵을 로드한다.',['cubemap','hdr','loader']],['CreateCube','큐브맵 TextureImage를 생성한다.',['cubemap','texture']],['LoadThumbnail','에디터 표시용 썸네일 이미지를 생성한다.',['thumbnail','editor']],['CreateChecker','체커보드 텍스처를 생성한다.',['procedural','texture']]],
'Graphics/TextureLoader.h':[['TextureImage','CPU 측 텍스처 픽셀 데이터와 포맷 정보를 담는 구조체.',['texture','data-model'],C]],
'Graphics/VertexTypes.h':[['VERTEX','위치·법선·UV·탄젠트 등을 가진 표준 정점 구조체.',['vertex','data-model'],C],['MakeVertexLayoutPCNTT','PCNTT 정점의 입력 레이아웃 설명을 만든다.',['input-layout','vertex']]],
'RHI/BindingTypes.h':[['BindingLayoutDesc','셰이더 단계별 리소스 슬롯 레이아웃 설명자.',['binding','rhi'],C],['ResourceSetDesc','레이아웃에 맞춰 실제 리소스를 묶는 리소스 셋 설명자.',['binding','rhi'],C],['BindingSlot','단일 바인딩 슬롯 설명.',['binding','rhi'],C],['ResourceBinding','슬롯과 실제 리소스를 연결하는 항목.',['binding','rhi'],C]],
'RHI/CommandList.h':[['CommandList','렌더 패스, PSO, 드로우, 배리어 등을 기록하는 추상 인터페이스.',['rhi','abstract-interface','command-list'],C]],
'RHI/D3D11/D3D11CommandList.cpp':[['BeginRenderPass','렌더 타깃/뎁스를 바인딩하고 클리어 및 뷰포트를 설정한다.',['render-pass','d3d11']],['SetResourceSet','리소스 셋의 리소스를 단계별로 바인딩한다.',['binding','d3d11']],['SetPipelineState','파이프라인 상태를 D3D11 컨텍스트에 적용한다.',['pso','d3d11']],['Barrier','D3D11에서 리소스 전환을 모사한다.',['barrier','d3d11']],['CopyBuffer','GPU 버퍼 간 복사를 수행한다.',['copy','d3d11']]],
'RHI/D3D11/D3D11CommandList.h':[['D3D11CommandList','CommandList의 D3D11 구현 클래스.',['rhi','d3d11','command-list'],C]],
'RHI/D3D11/D3D11Convert.cpp':[['ToDXGI','RHI Format을 DXGI_FORMAT으로 변환한다.',['conversion','dxgi']]],
'RHI/D3D11/D3D11Device.cpp':[['InitDirect3D','D3D11 디바이스, 스왑체인, 디버그 레이어를 생성한다.',['device','swapchain','initialization']],['CreateBuffer','RHI 설명자로 D3D11 버퍼를 생성한다.',['buffer','resource-creation']],['CreateTexture','텍스처와 뷰를 생성한다.',['texture','resource-creation']],['CreateSampler','샘플러 상태를 생성한다.',['sampler','resource-creation']],['CreateShader','컴파일된 바이트코드로 셰이더 객체를 만든다.',['shader','resource-creation']],['CreateBindingLayout','바인딩 레이아웃을 생성한다.',['binding','resource-creation']],['CreateResourceSet','레이아웃에 맞는 리소스 셋을 만든다.',['binding','resource-creation']],['UpdateBuffer','버퍼 내용을 갱신한다.',['buffer','upload']],['Resize','스왑체인과 백버퍼를 리사이즈한다.',['swapchain','resize']],['BeginFrame','프레임 시작 처리를 수행한다.',['frame','device']],['EndFrame','Present와 프레임 종료 처리를 수행한다.',['frame','present']],['ReadBackBuffer','GPU 버퍼를 리드백한다.',['readback','gpu']],['ReleaseDevice','모든 D3D 리소스를 해제한다.',['cleanup','device']],['ResolveTimestamps','GPU 타임스탬프 쿼리를 해석한다.',['profiling','gpu-timing']]],
};
const imp={
'Graphics/Mesh.cpp':['Graphics/Mesh.h'],'Graphics/Mesh.h':['Graphics/VertexTypes.h'],'Graphics/Model.cpp':['Graphics/Model.h','Core/Log.h'],'Graphics/Model.h':['Graphics/Mesh.h','Scene/Material.h'],
'Graphics/Renderer.cpp':['Graphics/Renderer.h','RHI/Device.h','RHI/CommandList.h','RHI/ShaderCompiler.h','RHI/ResourceDesc.h','RHI/BindingTypes.h','Graphics/ShaderConstants.h','Graphics/VertexTypes.h','Graphics/TextureLoader.h','Graphics/Mesh.h','Scene/Scene.h','Scene/Camera.h','Core/Paths.h','Core/Log.h','Core/Profiler.h'],
'Graphics/Renderer.h':['RHI/Handle.h','RHI/PipelineTypes.h','RHI/RenderPassTypes.h','Graphics/VertexTypes.h','Scene/Material.h','Scene/Light.h'],
'Graphics/Screenshot.cpp':['Graphics/Screenshot.h','Core/Log.h'],'Graphics/ShaderConstants.h':['Scene/Light.h'],'Graphics/TextureLoader.cpp':['Graphics/TextureLoader.h','Core/Log.h'],'Graphics/TextureLoader.h':['RHI/ResourceDesc.h'],'Graphics/VertexTypes.h':['RHI/PipelineTypes.h'],
'RHI/BindingTypes.h':['RHI/Handle.h','RHI/ResourceDesc.h'],'RHI/CommandList.h':['RHI/Handle.h','RHI/PipelineTypes.h','RHI/RenderPassTypes.h'],
'RHI/D3D11/D3D11CommandList.cpp':['RHI/D3D11/D3D11CommandList.h','RHI/D3D11/D3D11Device.h','RHI/D3D11/D3D11Convert.h','Core/Log.h'],
'RHI/D3D11/D3D11CommandList.h':['RHI/D3D11/D3D11Common.h','RHI/CommandList.h','RHI/BindingTypes.h'],
'RHI/D3D11/D3D11Convert.cpp':['RHI/D3D11/D3D11Convert.h','Core/Log.h'],'RHI/D3D11/D3D11Convert.h':['RHI/D3D11/D3D11Common.h','RHI/PipelineTypes.h','RHI/ResourceDesc.h'],
'RHI/D3D11/D3D11Device.cpp':['RHI/D3D11/D3D11Device.h','RHI/D3D11/D3D11Convert.h','Core/Log.h'],
'Core/Time.cpp':['Core/Time.h'],'Core/Time.h':['Core/EngineApi.h','Core/GameTimer.h'],
'Game/Components/FollowTarget.cpp':['Scene/Behaviour.h','Scene/GameObject.h','Scene/Scene.h'],
'Game/Components/Rigidbody.cpp':['Game/Components/Rigidbody.h','Scene/GameObject.h'],'Game/Components/Rigidbody.h':['Core/EngineApi.h','Scene/Behaviour.h'],
};
const nodes=[],edges=[];const ids=new Set();
const E=(s,t,ty,w)=>edges.push({source:s,target:t,type:ty,direction:'forward',weight:w});
for(const k of Object.keys(F)){
 const p=P+k;const [sum,tags,cx]=F[k];const fid='file:'+p;
 nodes.push({id:fid,type:'file',name:k.split('/').pop(),filePath:p,summary:sum,tags,complexity:cx});ids.add(fid);
 const ex=by[p]||{};
 const find=n=>[...(ex.functions||[]),...(ex.classes||[])].find(x=>x.name===n);
 for(const [n,s,t,kind] of (S[k]||[])){
  const x=find(n);const isC=kind===C;
  const id=(isC?'class:':'function:')+p+':'+n;if(ids.has(id))continue;ids.add(id);
  const nd={id,type:isC?'class':'function',name:n,filePath:p,summary:s,tags:t,complexity:'moderate'};
  nd.lineRange=x?[x.startLine,x.endLine]:[1,ex.totalLines||1];
  const len=nd.lineRange[1]-nd.lineRange[0];nd.complexity=len>80?'complex':len>25?'moderate':'simple';
  nodes.push(nd);E(fid,id,'contains',1.0);
 }
 for(const i of (imp[k]||[]))E(fid,'file:'+P+i,'imports',0.7);
}
E('file:'+P+'Graphics/Renderer.cpp','file:'+P+'RHI/D3D11/D3D11Device.cpp','depends_on',0.6);
E('class:'+P+'RHI/D3D11/D3D11CommandList.h:D3D11CommandList','class:'+P+'RHI/CommandList.h:CommandList','inherits',0.9);
fs.writeFileSync('.ua/intermediate/batch-8.json',JSON.stringify({nodes,edges},null,1));
console.log(nodes.length,edges.length);
