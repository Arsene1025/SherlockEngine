#pragma once
class Device;

class Shader
{
public:
	
	bool Initialize(Device* device);

	bool ShaderCreate();
	void ShaderUpdate();
	bool ShaderLoad();
	HRESULT ShaderLoad(const TCHAR* fxname, const CHAR* entry, const CHAR* target, ComPtr<ID3D11VertexShader>& outVS, ComPtr<ID3DBlob>& outCode);
	HRESULT ShaderLoad(const TCHAR* fxname, const CHAR* entry, const CHAR* target, ComPtr<ID3D11PixelShader>& outPS);
																					//out
	HRESULT ShaderCompile(const TCHAR* FileName, const CHAR* EntryPoint, const CHAR* ShaderModel, ComPtr<ID3DBlob>& outCode);

	// 아래 게터는 소유하지 않는 참조를 돌려준다. 수명은 Shader가 관리한다.
	ID3D11VertexShader* GetVertexShader() const { return pVS.Get(); }
	ID3D11PixelShader* GetPixtexShader() const { return pPS.Get(); }
	ID3DBlob* GetVSCode() const { return pVSCode.Get(); }
	ID3D11Buffer* GetCBBuffer() { return pCB.Get(); }
	ID3D11Buffer* GetLightCBBuffer() { return pLightCB.Get(); }

private:

public:

private:
	//셰이더
	ComPtr<ID3D11VertexShader> pVS;
	ComPtr<ID3D11PixelShader> pPS;

	//버텍스 셰이더 컴파일 코드 임시
	ComPtr<ID3DBlob> pVSCode;

	//상수버퍼
	ComPtr<ID3D11Buffer> pCB;
	ComPtr<ID3D11Buffer> pLightCB;

	//Device (소유하지 않는 참조)
	Device* graphicsDevice = nullptr;
};

