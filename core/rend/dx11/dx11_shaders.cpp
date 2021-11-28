/*
	Copyright 2021 flyinghead

	This file is part of Flycast.

    Flycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Flycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Flycast.  If not, see <https://www.gnu.org/licenses/>.
*/
#include "dx11_shaders.h"

const char * const VertexShader = R"(
struct VertexIn
{
	float4 pos : POSITION;
	float4 col : COLOR0;
	float4 spec : COLOR1;
	float2 uv : TEXCOORD0;
};

struct VertexOut
{
	float4 pos : SV_POSITION;
	float4 uv : TEXCOORD0;
	float4 col : COLOR0;
	float4 spec : COLOR1;
};

cbuffer constantBuffer : register(b0)
{
	float4x4 transMatrix;
};

VertexOut main(in VertexIn vin)
{
	VertexOut vo;
	vo.pos = mul(transMatrix, float4(vin.pos.xyz, 1.f));
#if pp_Gouraud == 1
	vo.col = vin.col * vo.pos.z;
	vo.spec = vin.spec * vo.pos.z;
#else
	// flat shading: no interpolation
	vo.col = vin.col;
	vo.spec = vin.spec;
#endif
	vo.uv = float4(vin.uv * vo.pos.z, 0.f, vo.pos.z);

	vo.pos.w = 1.f;
	vo.pos.z = 0.f;

	return vo;
}

)";

const char * const ModVolVertexShader = R"(
struct VertexIn
{
	float4 pos : POSITION;
};

struct VertexOut
{
	float4 pos : SV_POSITION;
	float4 uv : TEXCOORD0;
};

cbuffer constantBuffer : register(b0)
{
	float4x4 transMatrix;
};

VertexOut main(in VertexIn vin)
{
	VertexOut vo;
	vo.pos = mul(transMatrix, float4(vin.pos.xyz, 1.f));
	vo.uv = float4(0.f, 0.f, 0.f, vo.pos.z);

	vo.pos.w = 1.f;
	vo.pos.z = 0.f;

	return vo;
}

)";

const char * const PixelShader = R"(

#define PI 3.1415926f

struct Pixel 
{
	float4 pos : SV_POSITION;
	float4 uv : TEXCOORD0;
	float4 col : COLOR0;
	float4 spec : COLOR1;
};

Texture2D texture0 : register(t0);
sampler sampler0 : register(s0);

Texture2D paletteTexture : register(t1);
sampler paletteSampler : register(s1);

Texture2D fogTexture : register(t2);
sampler fogSampler : register(s2);

cbuffer constantBuffer : register(b0)
{
	float4 colorClampMin;
	float4 colorClampMax;
	float4 FOG_COL_VERT;
	float4 FOG_COL_RAM;
	float fogDensity;
	float fogScale;
	float alphaTestValue;
};

cbuffer polyConstantBuffer : register(b1)
{
	float paletteIndex;
	float trilinearAlpha;
};

float fog_mode2(float w)
{
	float z = clamp(w * fogDensity, 1.0f, 255.9999f);
	float exp = floor(log2(z));
	float m = z * 16.0f / pow(2.0, exp) - 16.0f;
	float idx = floor(m) + exp * 16.0f + 0.5f;
	float4 fogCoef = fogTexture.Sample(fogSampler, float2(idx / 128.0f, 0.75f - (m - floor(m)) / 2.0f));
	return fogCoef.a;
}

float4 clampColor(float4 color)
{
#if FogClamping == 1
	return clamp(color, colorClampMin, colorClampMax);
#else
	return color;
#endif
}

#if pp_Palette == 1

float4 palettePixel(float2 coords)
{
	int colorIdx = int(floor(texture0.Sample(sampler0, coords).a * 255.0f + 0.5f) + paletteIndex.x);
    float2 c = float2((fmod(float(colorIdx), 32.0f) * 2.0f + 1.0f) / 64.0f, (float(colorIdx / 32) * 2.0f + 1.0f) / 64.0f);
	return paletteTexture.Sample(paletteSampler, c);
}

#endif

struct PSO
{
	float4 col : SV_TARGET;
	float z : SV_DEPTH;
};

PSO main(in Pixel inpix)
{ 
#if pp_Gouraud == 1
	float4 color = inpix.col / inpix.uv.w;
	#if pp_BumpMap == 1 || pp_Offset == 1
		float4 specular = inpix.spec / inpix.uv.w;
	#endif
#else
	float4 color = inpix.col;
	#if pp_BumpMap == 1 || pp_Offset == 1
		float4 specular = inpix.spec;
	#endif
#endif
	#if pp_UseAlpha == 0
		color.a = 1.0f;
	#endif
	#if pp_FogCtrl == 3
		color = float4(FOG_COL_RAM.rgb, fog_mode2(inpix.uv.w));
	#endif
	#if pp_Texture == 1
	{
		float2 uv = inpix.uv.xy / inpix.uv.w;
		#if pp_Palette == 0
			float4 texcol = texture0.Sample(sampler0, uv);
		#else
			float4 texcol = palettePixel(uv);
		#endif
		
		#if pp_BumpMap == 1
			float s = PI / 2.0f * (texcol.a * 15.0f * 16.0f + texcol.r * 15.0f) / 255.0f;
			float r = 2.0f * PI * (texcol.g * 15.0f * 16.0f + texcol.b * 15.0f) / 255.0f;
			texcol[3] = clamp(specular.a + specular.r * sin(s) + specular.g * cos(s) * cos(r - 2.0f * PI * specular.b), 0.0f, 1.0f);
			texcol.rgb = float3(1.0f, 1.0f, 1.0f);	
		#else
			#if pp_IgnoreTexA == 1
				texcol.a = 1.0f;
			#endif
			#if cp_AlphaTest == 1
				if (alphaTestValue > texcol.a)
					discard;
				texcol.a = 1.0f;
			#endif
		#endif
		#if pp_ShadInstr == 0
			color = texcol;
		#endif
		#if pp_ShadInstr == 1
			color.rgb *= texcol.rgb;
			color.a = texcol.a;
		#endif
		#if pp_ShadInstr == 2
			color.rgb = lerp(color.rgb, texcol.rgb, texcol.a);
		#endif
		#if  pp_ShadInstr == 3
			color *= texcol;
		#endif
		
		#if pp_Offset == 1 && pp_BumpMap == 0
			color.rgb += specular.rgb;
		#endif
	}
	#endif
	
	color = clampColor(color);
	
	#if pp_FogCtrl == 0
		color.rgb = lerp(color.rgb, FOG_COL_RAM.rgb, fog_mode2(inpix.uv.w)); 
	#endif
	#if pp_FogCtrl == 1 && pp_Offset == 1 && pp_BumpMap == 0
		color.rgb = lerp(color.rgb, FOG_COL_VERT.rgb, specular.a);
	#endif
	
	#if pp_TriLinear == 1
	color *= trilinearAlpha;
	#endif

	PSO pso;
	float w = inpix.uv.w * 100000.0f;
	pso.z = log2(1.0f + w) / 34.0f;
	pso.col = color;

	return pso;
}

struct MVPixel 
{
	float4 pos : SV_POSITION;
	float4 uv : TEXCOORD0;
};

PSO modifierVolume(in MVPixel inpix)
{
	PSO pso;
	float w = inpix.uv.w * 100000.0f;
	pso.z = log2(1.0f + w) / 34.0f;
	pso.col = float4(0, 0, 0, fogScale);

	return pso;
}
)";

const char * const QuadVertexShader = R"(
struct VertexIn
{
	float2 pos : POSITION;
	float2 uv : TEXCOORD0;
};

struct VertexOut
{
	float4 pos : SV_POSITION;
	float2 uv : TEXCOORD0;
};

VertexOut main(in VertexIn vin)
{
	VertexOut vo;
#if ROTATE == 0
	vo.pos = float4(vin.pos, 0.f, 1.f);
#else
	vo.pos = float4(-vin.pos.y, vin.pos.x, 0.f, 1.f);
#endif
	vo.uv = vin.uv;

	return vo;
}
)";

const char * const QuadPixelShader = R"(
struct VertexIn
{
	float4 pos : SV_POSITION;
	float2 uv : TEXCOORD0;
};

sampler sampler0;
Texture2D texture0;

float4 main(in VertexIn vin) : SV_Target
{
	return texture0.Sample(sampler0, vin.uv);
}

)";

const char * const MacroValues[] { "0", "1", "2", "3" };

static D3D_SHADER_MACRO VertexMacros[]
{
	{ "pp_Gouraud", "1" },
	{ nullptr, nullptr }
};

enum PixelMacroEnum {
	MacroGouraud,
	MacroTexture,
	MacroUseAlpha,
	MacroIgnoreTexA,
	MacroShadInstr,
	MacroOffset,
	MacroFogCtrl,
	MacroBumpMap,
	MacroFogClamping,
	MacroTriLinear,
	MacroPalette,
	MacroAlphaTest
};

static D3D_SHADER_MACRO PixelMacros[]
{
	{ "pp_Gouraud", "1" },
	{ "pp_Texture", "0" },
	{ "pp_UseAlpha", "0" },
	{ "pp_IgnoreTexA", "0" },
	{ "pp_ShadInstr", "0" },
	{ "pp_Offset", "0" },
	{ "pp_FogCtrl", "0" },
	{ "pp_BumpMap", "0" },
	{ "FogClamping", "0" },
	{ "pp_TriLinear", "0" },
	{ "pp_Palette", "0" },
	{ "cp_AlphaTest", "0" },
	{ nullptr, nullptr }
};

const ComPtr<ID3D11PixelShader>& DX11Shaders::getShader(bool pp_Texture, bool pp_UseAlpha, bool pp_IgnoreTexA, u32 pp_ShadInstr,
		bool pp_Offset, u32 pp_FogCtrl, bool pp_BumpMap, bool fog_clamping,
		bool trilinear, bool palette, bool gouraud, bool alphaTest)
{
	const u32 hash = (int)pp_Texture
			| (pp_UseAlpha << 1)
			| (pp_IgnoreTexA << 2)
			| (pp_ShadInstr << 3)
			| (pp_Offset << 5)
			| (pp_FogCtrl << 6)
			| (pp_BumpMap << 8)
			| (fog_clamping << 9)
			| (trilinear << 10)
			| (palette << 11)
			| (gouraud << 12)
			| (alphaTest << 13);
	auto& shader = shaders[hash];
	if (shader == nullptr)
	{
		verify(pp_ShadInstr < ARRAY_SIZE(MacroValues));
		verify(pp_FogCtrl < ARRAY_SIZE(MacroValues));
		PixelMacros[MacroGouraud].Definition = MacroValues[gouraud];
		PixelMacros[MacroTexture].Definition = MacroValues[pp_Texture];
		PixelMacros[MacroUseAlpha].Definition = MacroValues[pp_UseAlpha];
		PixelMacros[MacroIgnoreTexA].Definition = MacroValues[pp_IgnoreTexA];
		PixelMacros[MacroShadInstr].Definition = MacroValues[pp_ShadInstr];
		PixelMacros[MacroOffset].Definition = MacroValues[pp_Offset];
		PixelMacros[MacroFogCtrl].Definition = MacroValues[pp_FogCtrl];
		PixelMacros[MacroBumpMap].Definition = MacroValues[pp_BumpMap];
		PixelMacros[MacroFogClamping].Definition = MacroValues[fog_clamping];
		PixelMacros[MacroTriLinear].Definition = MacroValues[trilinear];
		PixelMacros[MacroPalette].Definition = MacroValues[palette];
		PixelMacros[MacroAlphaTest].Definition = MacroValues[alphaTest];

		shader = compilePS(PixelShader, "main", PixelMacros);
		verify(shader != nullptr);
	}
	return shader;
}

const ComPtr<ID3D11VertexShader>& DX11Shaders::getVertexShader(bool gouraud)
{
	ComPtr<ID3D11VertexShader>& vertexShader = gouraud ? gouraudVertexShader : flatVertexShader;
	if (!vertexShader)
	{
		VertexMacros[0].Definition = MacroValues[gouraud];
		vertexShader = compileVS(VertexShader, "main", VertexMacros);
	}

	return vertexShader;
}

const ComPtr<ID3D11VertexShader>& DX11Shaders::getMVVertexShader()
{
	if (!modVolVertexShader)
		modVolVertexShader = compileVS(ModVolVertexShader, "main", nullptr);

	return modVolVertexShader;
}

const ComPtr<ID3D11PixelShader>& DX11Shaders::getModVolShader()
{
	if (!modVolShader)
		modVolShader = compilePS(PixelShader, "modifierVolume", PixelMacros);

	return modVolShader;
}

const ComPtr<ID3D11VertexShader>& DX11Shaders::getQuadVertexShader(bool rotate)
{
	ComPtr<ID3D11VertexShader>& shader = rotate ? quadRotateVertexShader : quadVertexShader;
	if (!shader)
	{
		D3D_SHADER_MACRO macros[]
		{
			{ "ROTATE", rotate ? "1" : "0" },
			{ nullptr, nullptr }
		};
		shader = compileVS(QuadVertexShader, "main", macros);
	}

	return shader;
}

const ComPtr<ID3D11PixelShader>& DX11Shaders::getQuadPixelShader()
{
	if (!quadPixelShader)
		quadPixelShader = compilePS(QuadPixelShader, "main", nullptr);

	return quadPixelShader;
}

ComPtr<ID3DBlob> DX11Shaders::compileShader(const char* source, const char* function, const char* profile, const D3D_SHADER_MACRO *pDefines)
{
	ComPtr<ID3DBlob> shaderBlob;
	ComPtr<ID3DBlob> errorBlob;
	if (FAILED(D3DCompile(source, strlen(source), nullptr, pDefines, nullptr, function, profile, 0, 0, &shaderBlob.get(), &errorBlob.get())))
		ERROR_LOG(RENDERER, "Shader compilation failed: %s", errorBlob->GetBufferPointer());

	return shaderBlob;
}

ComPtr<ID3D11VertexShader> DX11Shaders::compileVS(const char* source, const char* function, const D3D_SHADER_MACRO *pDefines)
{
	ComPtr<ID3DBlob> blob = compileShader(source, function, "vs_4_0", pDefines);
	ComPtr<ID3D11VertexShader> shader;
	if (blob)
	{
		if (FAILED(device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &shader.get())))
			ERROR_LOG(RENDERER, "Vertex shader creation failed");
	}

	return shader;
}

ComPtr<ID3D11PixelShader> DX11Shaders::compilePS(const char* source, const char* function, const D3D_SHADER_MACRO *pDefines)
{
	ComPtr<ID3DBlob> blob = compileShader(source, function, "ps_4_0", pDefines);
	ComPtr<ID3D11PixelShader> shader;
	if (blob)
	{
		if (device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &shader.get()) != S_OK)
			ERROR_LOG(RENDERER, "Pixel shader creation failed");
	}

	return shader;
}

ComPtr<ID3DBlob> DX11Shaders::getVertexShaderBlob()
{
	VertexMacros[0].Definition = MacroValues[0];
	return compileShader(VertexShader, "main", "vs_4_0", VertexMacros);
}

ComPtr<ID3DBlob> DX11Shaders::getMVVertexShaderBlob()
{
	return compileShader(ModVolVertexShader, "main", "vs_4_0", nullptr);
}

ComPtr<ID3DBlob> DX11Shaders::getQuadVertexShaderBlob()
{
	return compileShader(QuadVertexShader, "main", "vs_4_0", nullptr);
}
