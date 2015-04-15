// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once

#include "ShaderGenCommon.h"
#include "VideoCommon/NativeVertexFormat.h"
#include "VideoCommon/XFMemory.h"


#define LIGHT_COL "%s[5*%d].%s"
#define LIGHT_COL_PARAMS(lightsName, index, swizzle) (lightsName), (index), (swizzle)

#define LIGHT_COSATT "%s[5*%d+1]"
#define LIGHT_COSATT_PARAMS(lightsName, index) (lightsName), (index)

#define LIGHT_DISTATT "%s[5*%d+2]"
#define LIGHT_DISTATT_PARAMS(lightsName, index) (lightsName), (index)

#define LIGHT_POS "%s[5*%d+3]"
#define LIGHT_POS_PARAMS(lightsName, index) (lightsName), (index)

#define LIGHT_DIR "%s[5*%d+4]"
#define LIGHT_DIR_PARAMS(lightsName, index) (lightsName), (index)

/**
* Common uid data used for shader generators that use lighting calculations.
*/
#pragma pack(1)
struct LightingUidData
{
	u32 matsource : 4; // 4x1 bit
	u32 enablelighting : 4; // 4x1 bit
	u32 ambsource : 4; // 4x1 bit
	u32 pad0 : 4;
	u32 diffusefunc : 8; // 4x2 bits
	u32 attnfunc : 8; // 4x2 bits	
	u32 light_mask : 32; // 4x8 bits
};
#pragma pack()

template<class T, bool Write_Code>
static void GenerateLightShader(T& object,
	LightingUidData& uid_data,
	int index,
	int litchan_index,
	const char* lightsName,
	int coloralpha,
	const XFMemory &xfr)
{
	const LitChannel& chan = (litchan_index > 1) ? xfr.alpha[litchan_index - 2] : xfr.color[litchan_index];
	uid_data.attnfunc |= chan.attnfunc << (2 * litchan_index);
	uid_data.diffusefunc |= chan.diffusefunc << (2 * litchan_index);
	if (Write_Code)
	{
		const char* swizzle = "xyzw";
		if (coloralpha == 1)
			swizzle = "xyz";
		else if (coloralpha == 2)
			swizzle = "w";

		object.Write("ldir = " LIGHT_POS".xyz - pos.xyz;\n",
			LIGHT_POS_PARAMS(lightsName, index));

		object.Write("if (dot(ldir,ldir) < 0.00001)\n\t ldir = _norm0;\n");
		switch (chan.attnfunc)
		{
		case LIGHTATTN_NONE:
		case LIGHTATTN_DIR:
			object.Write("ldir = normalize(ldir);\n");
			object.Write("attn = 1.0f;\n");
			break;
		case LIGHTATTN_SPEC:
			object.Write("ldir = normalize(ldir);\n");
			object.Write("attn = (dot(_norm0, ldir) >= 0.0) ? max(0.0, dot(_norm0, " LIGHT_DIR".xyz)) : 0.0;\n", LIGHT_DIR_PARAMS(lightsName, index));
			object.Write("attn = max(0.0f, dot(" LIGHT_COSATT".xyz, float3(1.0, attn, attn*attn))) / dot(%s(" LIGHT_DISTATT".xyz), float3(1.0, attn, attn*attn));\n",
				LIGHT_COSATT_PARAMS(lightsName, index),
				(chan.diffusefunc == LIGHTDIF_NONE) ? "" : "normalize",
				LIGHT_DISTATT_PARAMS(lightsName, index));
			break;
		case LIGHTATTN_SPOT:
			object.Write("dist2 = dot(ldir, ldir);\n"
				"dist = sqrt(dist2);\n"
				"ldir = ldir / dist;\n"
				"attn = max(0.0, dot(ldir, " LIGHT_DIR".xyz));\n", LIGHT_DIR_PARAMS(lightsName, index));
			// attn*attn may overflow
			object.Write("attn = max(0.0, dot(" LIGHT_COSATT".xyz, float3(1.0, attn, attn*attn))) / dot(" LIGHT_DISTATT".xyz, float3(1.0,dist,dist2));\n",
				LIGHT_COSATT_PARAMS(lightsName, index), LIGHT_DISTATT_PARAMS(lightsName, index));
			break;
		}

		switch (chan.diffusefunc)
		{
		case LIGHTDIF_NONE:
			object.Write("lacc.%s += attn * " LIGHT_COL";\n", swizzle, LIGHT_COL_PARAMS(lightsName, index, swizzle));
			break;
		case LIGHTDIF_SIGN:
		case LIGHTDIF_CLAMP:
			object.Write("lacc.%s += attn * %sdot(ldir, _norm0)) * " LIGHT_COL";\n",
				swizzle,
				chan.diffusefunc != LIGHTDIF_SIGN ? "max(0.0," : "(",
				LIGHT_COL_PARAMS(lightsName, index, swizzle));
			break;
		default: _assert_(0);
		}
		object.Write("\n");
	}
}

// vertex shader
// lights/colors
// materials name is I_MATERIALS in vs and I_PMATERIALS in ps
// inColorName is color in vs and colors_ in ps
// dest is o.colors_ in vs and colors_ in ps
template<class T, bool Write_Code>
static void GenerateLightingShader(T& object, LightingUidData& uid_data, int components, const char* materialsName, const char* lightsName, const char* inColorName, const char* dest, const  XFMemory &xfr, bool use_integer_math)
{
	for (unsigned int j = 0; j < xfr.numChan.numColorChans; j++)
	{
		const LitChannel& color = xfr.color[j];
		const LitChannel& alpha = xfr.alpha[j];
		uid_data.matsource |= xfr.color[j].matsource << j;
		if (Write_Code)
		{
			object.Write("{\n");
			if (color.matsource) // from vertex
			{
				if (components & (VB_HAS_COL0 << j))
					object.Write("mat = round(%s%d * 255.0);\n", inColorName, j);
				else if (components & VB_HAS_COL0)
					object.Write("mat = round(%s0 * 255.0);\n", inColorName);
				else
					object.Write("mat = float4(255.0,255.0,255.0,255.0);\n");
			}
			else // from color
			{
				object.Write("mat = %s[%d];\n", materialsName, j + 2);
			}
		}

		uid_data.enablelighting |= xfr.color[j].enablelighting << j;
		if (color.enablelighting)
		{
			uid_data.ambsource |= xfr.color[j].ambsource << j;
			if (Write_Code)
			{
				if (color.ambsource) // from vertex
				{
					if (components & (VB_HAS_COL0 << j))
						object.Write("lacc = round(%s%d * 255.0);\n", inColorName, j);
					else if (components & VB_HAS_COL0)
						object.Write("lacc = round(%s0 * 255.0);\n", inColorName);
					else
						// TODO: this isn't verified. Here we want to read the ambient from the vertex,
						// but the vertex itself has no color. So we don't know which value to read.
						// Returing 1.0 is the same as disabled lightning, so this could be fine
						object.Write("lacc = float4(255.0,255.0,255.0,255.0);\n");
				}
				else // from color
				{
					object.Write("lacc = %s[%d];\n", materialsName, j);
				}
			}
		}
		else if (Write_Code)
		{
			object.Write("lacc = float4(255.0,255.0,255.0,255.0);\n");
		}

		// check if alpha is different
		uid_data.matsource |= xfr.alpha[j].matsource << (j + 2);
		if (Write_Code)
		{
			if (alpha.matsource != color.matsource)
			{
				if (alpha.matsource) // from vertex
				{
					if (components & (VB_HAS_COL0 << j))
						object.Write("mat.w = round(%s%d.w * 255.0);\n", inColorName, j);
					else if (components & VB_HAS_COL0)
						object.Write("mat.w = round(%s0.w * 255.0);\n", inColorName);
					else object.Write("mat.w = 255.0;\n");
				}
				else // from color
				{
					object.Write("mat.w = %s[%d].w;\n", materialsName, j + 2);
				}
			}
		}


		uid_data.enablelighting |= xfr.alpha[j].enablelighting << (j + 2);
		if (alpha.enablelighting)
		{
			uid_data.ambsource |= xfr.alpha[j].ambsource << (j + 2);
			if (Write_Code)
			{
				if (alpha.ambsource) // from vertex
				{
					if (components & (VB_HAS_COL0 << j))
						object.Write("lacc.w = round(%s%d.w * 255.0);\n", inColorName, j);
					else if (components & VB_HAS_COL0)
						object.Write("lacc.w = round(%s0.w * 255.0);\n", inColorName);
					else
						// TODO: The same for alpha: We want to read from vertex, but the vertex has no color
						object.Write("lacc.w = 255.0;\n");
				}
				else // from color
				{
					object.Write("lacc.w = %s[%d].w;\n", materialsName, j);
				}
			}
		}
		else if (Write_Code)
		{
			object.Write("lacc.w = 255.0;\n");
		}

		if (color.enablelighting && alpha.enablelighting)
		{
			// both have lighting, test if they use the same lights
			int mask = 0;
			uid_data.attnfunc |= color.attnfunc << (2 * j);
			uid_data.attnfunc |= alpha.attnfunc << (2 * (j + 2));
			uid_data.diffusefunc |= color.diffusefunc << (2 * j);
			uid_data.diffusefunc |= alpha.diffusefunc << (2 * (j + 2));
			uid_data.light_mask |= color.GetFullLightMask() << (8 * j);
			uid_data.light_mask |= alpha.GetFullLightMask() << (8 * (j + 2));
			if (color.lightparams == alpha.lightparams)
			{
				mask = color.GetFullLightMask() & alpha.GetFullLightMask();
				if (mask)
				{
					for (int i = 0; i < 8; ++i)
					{
						if (mask & (1 << i))
						{
							GenerateLightShader<T, Write_Code>(object, uid_data, i, j, lightsName, 3, xfr);
						}
					}
				}
			}

			// no shared lights
			for (int i = 0; i < 8; ++i)
			{
				if (!(mask&(1 << i)) && (color.GetFullLightMask() & (1 << i)))
					GenerateLightShader<T, Write_Code>(object, uid_data, i, j, lightsName, 1, xfr);
				if (!(mask&(1 << i)) && (alpha.GetFullLightMask() & (1 << i)))
					GenerateLightShader<T, Write_Code>(object, uid_data, i, j + 2, lightsName, 2, xfr);
			}
		}
		else if (color.enablelighting || alpha.enablelighting)
		{
			// lights are disabled on one channel so process only the active ones
			const LitChannel& workingchannel = color.enablelighting ? color : alpha;
			const int lit_index = color.enablelighting ? j : (j + 2);
			int coloralpha = color.enablelighting ? 1 : 2;

			uid_data.light_mask |= workingchannel.GetFullLightMask() << (8 * lit_index);
			for (int i = 0; i < 8; ++i)
			{
				if (workingchannel.GetFullLightMask() & (1 << i))
					GenerateLightShader<T, Write_Code>(object, uid_data, i, lit_index, lightsName, coloralpha, xfr);
			}
		}
		if (Write_Code)
		{
			if (use_integer_math)
			{
				object.Write("ilacc = int4(round(lacc));\n");
				object.Write("ilacc = clamp(ilacc, 0, 255);\n");
				object.Write("ilacc += ilacc >> 7;\n");
				object.Write("%s%d = float4((int4(mat) * ilacc) >> 8) / 255.0;\n", dest, j);
			}
			else
			{
				object.Write("lacc = clamp(lacc, 0.0, 255.0);\n");
				object.Write("lacc = lacc + floor(lacc / 128.0);\n");
				object.Write("%s%d = floor((mat * lacc)/256.0)/255.0;\n", dest, j);
			}			
			object.Write("}\n");
		}
	}
}
