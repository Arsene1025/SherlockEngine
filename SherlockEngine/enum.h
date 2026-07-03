#pragma once


enum
{
	RS_SOLID, //삼각형 채우기 - solid
	RS_WIREFRM, //삼각형 채우기 - wireframe
	RS_CULLBACK, //컬링 - ccw
	RS_WIRECULLBACK, //와이어 프레임 + 컬링
	RS_MAX_
};

enum 
{
	RM_SOLID = 0x0000,		// 삼각형채우기 - Solid
	RM_WIREFRAME = 0x0001,		// 삼각형채우기 -Wire-frame
	RM_CULLBACK = 0x0002,		// 뒷면 컬링 "CCW"
	//기본 solid + 컬링
	RM_DEFAULT = RM_SOLID | RM_CULLBACK,

};