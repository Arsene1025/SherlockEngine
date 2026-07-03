#pragma once


//Vertex구조체
struct VERTEX
{
	float x, y, z; 			//좌표(Position)
	float r, g, b, a;		//색상(Diffuse Color)
};

struct GameTime
{
    float deltaTime = 0.0f;
    float totalTime = 0.0f;

    float GetDeltaTimeMS() const
    {
        return deltaTime * 1000.0f;
    }
};

struct ConstBuffer
{
	XMMATRIX mWorld;
	XMMATRIX mView;
	XMMATRIX mProj;
	XMMATRIX mWVP;
};