const P='Projects/Sample/Scripts/';
const f=(n,s,t,c)=>({id:'file:'+P+n,type:'file',name:n,filePath:P+n,summary:s,tags:t,complexity:c});
const nodes=[
f('PlayerController.cpp','PlayerController 스크립트의 구현. 방향키 이동(Shift 가속), Space 점프, R 키로 다른 오브젝트의 Rotator 토글을 처리하고 Reflect로 속성을 노출함.',['script','gameplay','input','implementation'],'moderate'),
f('PlayerController.h','플레이어 스크립트 PlayerController 선언. Script를 상속하며 speed/boost/jumpSpeed 등 속성과 Rotator 전방 선언을 가짐. 새 스크립트 작성의 템플릿 역할.',['script','header','gameplay','template'],'simple'),
f('Rotator.cpp','Rotator 스크립트 구현. Reflect로 degreesPerSecond를 노출하고 매 프레임 Transform을 회전시킴.',['script','implementation','transform','gameplay'],'simple'),
f('Rotator.h','가장 짧은 예제 스크립트 Rotator 선언. 매 프레임 회전하며 Toggle/SetSpeed API를 다른 스크립트에 제공함.',['script','header','gameplay','example'],'simple'),
{id:'class:'+P+'PlayerController.h:PlayerController',type:'class',name:'PlayerController',filePath:P+'PlayerController.h',lineRange:[9,24],summary:'방향키 이동, 점프, Rotator 토글을 수행하는 플레이어 스크립트 클래스. Script를 상속하고 Reflect/Start/Update를 재정의함.',tags:['script','player','gameplay','class'],complexity:'simple'},
{id:'class:'+P+'Rotator.h:Rotator',type:'class',name:'Rotator',filePath:P+'Rotator.h',lineRange:[7,19],summary:'매 프레임 degreesPerSecond만큼 회전하는 Script 파생 클래스. Toggle과 SetSpeed로 외부 제어를 허용함.',tags:['script','rotation','gameplay','class'],complexity:'simple'},
{id:'function:'+P+'PlayerController.cpp:Update',type:'function',name:'Update',filePath:P+'PlayerController.cpp',lineRange:[23,49],summary:'매 프레임 입력을 읽어 XZ 이동, Shift 가속, 점프, R 키 Rotator 토글을 처리함.',tags:['input','movement','update-loop'],complexity:'moderate'},
{id:'function:'+P+'PlayerController.cpp:Start',type:'function',name:'Start',filePath:P+'PlayerController.cpp',lineRange:[15,21],summary:'이름으로 대상 오브젝트를 찾아 Rotator 참조를 얻고, 없으면 경고 로그를 남김.',tags:['lifecycle','lookup','logging'],complexity:'simple'},
{id:'function:'+P+'PlayerController.cpp:Reflect',type:'function',name:'Reflect',filePath:P+'PlayerController.cpp',lineRange:[7,13],summary:'speed, boost, jumpSpeed, rotatorObject 속성을 PropertyVisitor에 노출함.',tags:['reflection','serialization','properties'],complexity:'simple'},
];
const e=(s,t,ty,w)=>({source:s,target:t,type:ty,direction:'forward',weight:w});
const F=n=>'file:'+P+n;
const edges=[
e(F('PlayerController.cpp'),F('PlayerController.h'),'imports',0.7),
e(F('PlayerController.cpp'),F('Rotator.h'),'imports',0.7),
e(F('Rotator.cpp'),F('Rotator.h'),'imports',0.7),
e(F('PlayerController.h'),'class:'+P+'PlayerController.h:PlayerController','contains',1),
e(F('PlayerController.h'),'class:'+P+'PlayerController.h:PlayerController','exports',0.8),
e(F('Rotator.h'),'class:'+P+'Rotator.h:Rotator','contains',1),
e(F('Rotator.h'),'class:'+P+'Rotator.h:Rotator','exports',0.8),
...['Update','Start','Reflect'].map(n=>e(F('PlayerController.cpp'),'function:'+P+'PlayerController.cpp:'+n,'contains',1)),
e('function:'+P+'PlayerController.cpp:Update','class:'+P+'Rotator.h:Rotator','calls',0.8),
e('function:'+P+'PlayerController.cpp:Start','class:'+P+'Rotator.h:Rotator','calls',0.8),
e(F('PlayerController.cpp'),F('Rotator.cpp'),'related',0.5),
];
require('fs').writeFileSync('.ua/intermediate/batch-1.json',JSON.stringify({nodes,edges},null,1));
