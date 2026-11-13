#pragma once

class Scene;
class DebugDraw;

// 스켈레톤 디버그 표시: 씬의 스킨 오브젝트마다 조인트 월드 위치를 계산해 DebugDraw 에 선을 쌓음 (Animator 가 채운
// GameObject::SkeletonPose 를 읽음). mode: 0 끔, 1 본 선(부모 → 자식, 초록) + 관절 십자(노랑), 2 = 1 + 조인트 로컬 축(XYZ = 빨강·초록·파랑).
// Engine 이 Renderer::Render 앞에서 부름 — 에디터에서 씬 업데이트가 멈춰 있어도 마지막 포즈가 계속 보임.
void DrawSkeletonDebug(DebugDraw& draw, const Scene& scene, int mode);
