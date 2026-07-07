#pragma once
class Device;

class Shader
{
public:
	
	bool Initalize(Device* device);

	void ShaderCreate();
	void ShaderUpdate();
	void ShaderRelease();
	bool ShaderLoad();
	HRESULT ShaderLoad(const TCHAR* fxname, const CHAR* entry, const CHAR* target, ID3D11VertexShader** ppPS, ID3DBlob** ppCode = NULL);
	HRESULT ShaderLoad(const TCHAR* fxname, const CHAR* entry, const CHAR* target, ID3D11PixelShader** ppPS);
																					//out
	HRESULT ShaderCompile(const TCHAR* FileName, const CHAR* EntryPoint, const CHAR* ShaderModel, ID3DBlob** ppCode);

	ID3D11VertexShader* GetVertexShader() const { return pVS; }
	ID3D11PixelShader* GetPixtexShader() const { return pPS; }
	ID3DBlob* GetVSCode() const { return pVSCode; }
	ID3D11Buffer* GetCBBuffer() { return pCB; }

private:

public:

private:
	//셰이더
	ID3D11VertexShader* pVS = nullptr;
	ID3D11PixelShader* pPS = nullptr;

	//버텍스 셰이더 컴파일 코드 임시
	ID3DBlob* pVSCode = nullptr;

	//상수버퍼
	ID3D11Buffer* pCB = nullptr;

	//Device
	Device* graphicsDevice = nullptr;
};

