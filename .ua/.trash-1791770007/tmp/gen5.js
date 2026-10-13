const N=[],E=[];
const nm=p=>p.split('/').pop();
function f(type,p,summary,tags,cx){N.push({id:type+':'+p,type,name:nm(p),filePath:p,summary,tags,complexity:cx});}
function e(s,t,type,w){E.push({source:s,target:t,type,direction:'forward',weight:w});}
f('config','.claude/launch.json','docs와 저장소 루트를 로컬 python http.server(8765, 8766)로 띄우는 Claude 미리보기 실행 설정.',['configuration','dev-server','tooling'],'simple');
f('config','.ua/config.json','Understand-Anything 분석 결과의 출력 언어를 지정하는 설정 파일.',['configuration','tooling','analysis'],'simple');
const projs=[['CubemapTest','HDR 큐브맵 IBL 환경 테스트'],['QuaternionLookTest','쿼터니언 누적(카메라 roll) 실험'],['Sample','예제 씬과 스크립트를 담은 샘플']];
for(const [p,d] of projs){
 const b='Projects/'+p+'/';
 f('file',b+p+'.sherlock',p+' 프로젝트 매니페스트(JSON): 이름, 시작 씬, 엔진 루트, 포맷 버전을 정의해 에디터가 프로젝트를 열게 함.',['project-manifest','configuration','editor'],'simple');
 f('config',b+p+'.sln',p+' 스크립트 DLL 프로젝트를 담은 Visual Studio 솔루션.',['build-system','visual-studio','solution'],'simple');
 f('file',b+p+'Scripts.vcxproj',p+'의 게임 스크립트를 '+p+'Scripts.dll로 빌드하는 MSBuild C++ 프로젝트(핫리로드용).',['build-system','msbuild','script-dll','hot-reload'],'moderate');
 f('file',b+p+'Scripts.vcxproj.filters',p+'Scripts.vcxproj의 Visual Studio 솔루션 탐색기 폴더 필터 정의.',['build-system','msbuild','ide-metadata'],'simple');
 e('config:'+b+p+'.sln','file:'+b+p+'Scripts.vcxproj','depends_on',0.6);
 e('file:'+b+p+'Scripts.vcxproj.filters','file:'+b+p+'Scripts.vcxproj','configures',0.6);
 e('file:'+b+p+'.sherlock','file:'+b+p+'Scripts.vcxproj','related',0.5);
}
f('config','Projects/CubemapTest/Assets/Scenes/Main.json','CubemapTest의 PBR(버전 4) 씬: 카메라, HDR 큐브맵 IBL 환경, 조명, 메시, 재질, 오브젝트 정의.',['scene-data','pbr','ibl','cubemap'],'complex');
f('config','Projects/CubemapTest/imgui.ini','CubemapTest 에디터의 ImGui 창 배치와 도킹 상태 저장 파일.',['configuration','imgui','editor-layout'],'moderate');
f('config','Projects/Sample/Assets/Scenes/demo_components.json','Sample의 컴포넌트 데모 씬: 조명, 재질, 메시, 오브젝트와 환경 설정.',['scene-data','demo','pbr'],'complex');
f('config','Projects/Sample/Assets/Scenes/floor_cube.json','Sample의 바닥과 큐브로 이뤄진 단순 씬 데이터.',['scene-data','sample','pbr'],'moderate');
e('file:Projects/CubemapTest/CubemapTest.sherlock','config:Projects/CubemapTest/Assets/Scenes/Main.json','configures',0.6);
e('file:Projects/Sample/Sample.sherlock','config:Projects/Sample/Assets/Scenes/demo_components.json','configures',0.6);
// scripts
function rot(p){const b='Projects/'+p+'/Scripts/';
 f('file',b+'Rotator.h','Script를 상속한 Rotator 스크립트 선언(speed 필드). 에디터 New Script 템플릿에서 생성됨.',['script','header','gameplay'],'simple');
 f('file',b+'Rotator.cpp','Rotator의 Reflect/Start/Update 구현으로, 매 프레임 오브젝트를 회전시키는 스크립트.',['script','gameplay','component'],'simple');
 N.push({id:'class:'+b+'Rotator.h:Rotator',type:'class',name:'Rotator',filePath:b+'Rotator.h',lineRange:[8,17],summary:'speed 속성으로 오브젝트를 회전시키는 Script 파생 클래스.',tags:['script','component','behaviour'],complexity:'simple'});
 e('file:'+b+'Rotator.h','class:'+b+'Rotator.h:Rotator','contains',1);
 e('file:'+b+'Rotator.h','class:'+b+'Rotator.h:Rotator','exports',0.8);
 e('file:'+b+'Rotator.cpp','file:'+b+'Rotator.h','imports',0.7);
 e('file:'+b+'Rotator.cpp','class:'+b+'Rotator.h:Rotator','related',0.5);
}
rot('CubemapTest');rot('QuaternionLookTest');
const q='Projects/QuaternionLookTest/Scripts/Tests/';
f('file',q+'QuaternionLook.h','가상 마우스 원 운동으로 쿼터니언 누적 시 roll이 생기는 현상을 재현하는 실험 스크립트 선언.',['experiment','script','quaternion','header'],'simple');
f('file',q+'QuaternionLook.cpp','mode 0(로컬 누적)과 mode 1(월드 yaw+로컬 pitch)로 카메라 회전을 누적하고 roll을 로그로 남기는 실험 구현.',['experiment','script','quaternion','camera'],'moderate');
N.push({id:'class:'+q+'QuaternionLook.h:QuaternionLook',type:'class',name:'QuaternionLook',filePath:q+'QuaternionLook.h',lineRange:[15,33],summary:'가상 마우스가 그린 원의 증분을 쿼터니언에 곱해 홀로노미로 인한 roll 누적을 검증하는 Script 클래스.',tags:['experiment','quaternion','script'],complexity:'moderate'});
N.push({id:'function:'+q+'QuaternionLook.cpp:Update',type:'function',name:'Update',filePath:q+'QuaternionLook.cpp',lineRange:[30,67],summary:'프레임마다 원 위의 가상 마우스 증분을 mode에 따라 쿼터니언에 곱하고 Transform에 반영하며 roll을 주기적으로 로그.',tags:['experiment','quaternion','update-loop'],complexity:'moderate'});
e('file:'+q+'QuaternionLook.h','class:'+q+'QuaternionLook.h:QuaternionLook','contains',1);
e('file:'+q+'QuaternionLook.h','class:'+q+'QuaternionLook.h:QuaternionLook','exports',0.8);
e('file:'+q+'QuaternionLook.cpp','function:'+q+'QuaternionLook.cpp:Update','contains',1);
e('file:'+q+'QuaternionLook.cpp','file:'+q+'QuaternionLook.h','imports',0.7);
f('file','Projects/Sample/Scripts/Test.cpp','Sample 프로젝트의 Test 스크립트 구현(Reflect/Start/Update 골격).',['script','sample','gameplay'],'simple');
e('file:Projects/Sample/Scripts/Test.cpp','file:Projects/Sample/Scripts/Test.h','imports',0.7);
for(const n of N)if(!n.summary)throw n.id;
require('fs').writeFileSync('.ua/intermediate/batch-5.json',JSON.stringify({nodes:N,edges:E},null,1));
console.log(N.length,E.length,new Set(N.map(n=>n.id)).size);
