// godrays.cpp: ray-marched volumetric light shafts using shadow maps (screen-space evaluation).

#include "engine.h"

extern GLuint hdrfbo, mshdrfbo;
extern GLenum hdrformat;

extern int csmshadowmap, csmsplits, csmfarplane;
extern int smfilter, smgather, usetexgather;

namespace godRays
{
    VARP(godrays, 0, 1, 1);
    FVARP(godraysscale, 0.0625f, 0.5f, 1.0f);
    VARP(godraysblur, 0, 2, 3);
    VARP(godrayssteps, 1, 16, 256);
    VARR(godraysstrength, 0, 10, 200);
    VARR(godraysmax, 1, 2, 10);
    VARR(godraysskymax, 0, 2, 10);
    CVAR1R(godrayscolor, 0);

    static int targetWidth = -1, targetHeight = -1;
    static GLuint godRayFbo[2] = { 0, 0 }, godRayTex[2] = { 0, 0 };
    static Shader *godRayShader = NULL, *godRayUpsampleShader[2] = { NULL, NULL }, *godRayBilateralShader[2][2] = {{ NULL, NULL }, { NULL, NULL }};
    static int shaderVariantKey = -1, godRayBilateralKey[2] = { -1, -1 };
    void cleanup();

    static inline bool useGatherForShadowMap()
    {
        return smfilter > 1 && smgather && hasTG && usetexgather;
    }

    static int computeShaderVariantKey(bool useMsaa)
    {
        int key = clamp(csmsplits, 1, 8);
        key |= clamp(godrayssteps, 1, 256) << 4;
        key |= (useMsaa ? 1 : 0) << 12;
        key |= clamp(smfilter, 0, 3) << 13;
        key |= (useGatherForShadowMap() ? 1 : 0) << 16;
        key |= (usetexgather > 1 ? 1 : 0) << 17;
        return key;
    }

    static Shader *loadShader(bool useMsaa)
    {
        string options;
        int optionsLen = 0;

        if(useGatherForShadowMap()) options[optionsLen++] = smfilter > 2 ? 'G' : 'g';
        else if(smfilter) options[optionsLen++] = smfilter > 2 ? 'E' : (smfilter > 1 ? 'F' : 'f');
        else options[optionsLen++] = 'N';
        if(useMsaa) options[optionsLen++] = 'm';
        options[optionsLen] = '\0';

        int splitCount = clamp(csmsplits, 1, 8);
        int stepCount = clamp(godrayssteps, 1, 256);

        defformatstring(name, "godrays%s%d%d", options, splitCount, stepCount);
        return generateshader(name, "godraysshader \"%s\" %d %d", options, splitCount, stepCount);
    }

    static bool ensureShader(bool useMsaa)
    {
        int variantKey = computeShaderVariantKey(useMsaa);
        if(!godRayShader || shaderVariantKey != variantKey)
        {
            godRayShader = loadShader(useMsaa);
            shaderVariantKey = variantKey;
        }
        return godRayShader && godRayShader != nullshader;
    }

    static Shader *loadUpsampleShader(bool useMsaa)
    {
        defformatstring(name, "godraysupsample%d", useMsaa ? 1 : 0);
        return generateshader(name, "godraysupsampleshader %d", useMsaa ? 1 : 0);
    }

    static Shader *ensureUpsampleShader(bool useMsaa)
    {
        int shaderIndex = useMsaa ? 1 : 0;
        if(!godRayUpsampleShader[shaderIndex]) godRayUpsampleShader[shaderIndex] = loadUpsampleShader(useMsaa);
        return godRayUpsampleShader[shaderIndex];
    }

    static Shader *loadBilateralShader(bool useMsaa, int pass, int numTaps)
    {
        defformatstring(name, "godraysbilateral%c%d%d", 'x' + pass, useMsaa ? 1 : 0, numTaps);
        return generateshader(name, "godraysbilateralshader %d %d %d", useMsaa ? 1 : 0, numTaps, pass);
    }

    static bool ensureBilateralShaders(bool useMsaa, int numTaps)
    {
        int shaderIndex = useMsaa ? 1 : 0;
        numTaps = clamp(numTaps, 1, MAXBLURRADIUS);
        if(godRayBilateralKey[shaderIndex] != numTaps || !godRayBilateralShader[shaderIndex][0] || !godRayBilateralShader[shaderIndex][1])
        {
            loopi(2) godRayBilateralShader[shaderIndex][i] = loadBilateralShader(useMsaa, i, numTaps);
            godRayBilateralKey[shaderIndex] = numTaps;
        }
        return godRayBilateralShader[shaderIndex][0] && godRayBilateralShader[shaderIndex][0] != nullshader &&
               godRayBilateralShader[shaderIndex][1] && godRayBilateralShader[shaderIndex][1] != nullshader;
    }

    static int computeTargetSize(int sourceSize)
    {
        int scaledSize = int(ceilf(sourceSize * max(godraysscale, 1e-3f)));
        return clamp(scaledSize, 1, sourceSize);
    }

    static bool ensureBuffer()
    {
        int width = computeTargetSize(vieww);
        int height = computeTargetSize(viewh);
        if(godRayTex[0] && godRayFbo[0] && godRayTex[1] && godRayFbo[1] && width == targetWidth && height == targetHeight) return true;

        cleanup();
        targetWidth = width;
        targetHeight = height;

        loopi(2)
        {
            if(!godRayTex[i]) glGenTextures(1, &godRayTex[i]);
            if(!godRayFbo[i]) glGenFramebuffers_(1, &godRayFbo[i]);

            glBindFramebuffer_(GL_FRAMEBUFFER, godRayFbo[i]);
            createtexture(godRayTex[i], targetWidth, targetHeight, NULL, 3, 1, hdrformat, GL_TEXTURE_RECTANGLE);
            glBindTexture(GL_TEXTURE_RECTANGLE, godRayTex[i]);
            glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D_(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE, godRayTex[i], 0);
            if(glCheckFramebufferStatus_(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                fatal("failed allocating god ray buffer!");
        }
        glBindFramebuffer_(GL_FRAMEBUFFER, 0);
        return true;
    }

    bool shouldRender()
    {
        return godrays && godraysstrength > 0 && godraysscale > 0 && godrayssteps > 0 && !drawtex && csmshadowmap && !sunlight.iszero();
    }

    void render()
    {
        if(!shouldRender()) return;

        bool useMsaa = msaalight != 0;
        if(!ensureShader(useMsaa) || !ensureBuffer() || !bindSunlightShadowMap()) return;

        bool hadScissor = glIsEnabled(GL_SCISSOR_TEST) != 0;
        bool hadDepthTest = glIsEnabled(GL_DEPTH_TEST) != 0;
        bool hadBlend = glIsEnabled(GL_BLEND) != 0;

        if(hadScissor) glDisable(GL_SCISSOR_TEST);

        glBindFramebuffer_(GL_FRAMEBUFFER, godRayFbo[0]);
        glViewport(0, 0, targetWidth, targetHeight);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);

        glActiveTexture_(GL_TEXTURE3);
        if(useMsaa) glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, msdepthtex);
        else glBindTexture(GL_TEXTURE_RECTANGLE, gdepthtex);
        glActiveTexture_(GL_TEXTURE0);

        godRayShader->set();

        int stepCount = max(godrayssteps, 1);
        float maxDistance = max(float(csmfarplane), nearplane + 1.0f);
        float maxIntensity = max(godraysmax, 1) / 100.0f;
        float skyMaxIntensity = max(godraysskymax, 0) / 100.0f;
        LOCALPARAMF(godRayParams, maxDistance, 1.0f/stepCount, 1.0f/maxDistance, maxIntensity);
        LOCALPARAMF(godRaySkyMax, skyMaxIntensity);
        LOCALPARAMF(godRayScreen, float(vieww)/targetWidth, float(viewh)/targetHeight, float(targetWidth)/vieww, float(targetHeight)/viewh);

        vec rayColor = godrayscolor.iszero() ? sunlight.tocolor() : godrayscolor.tocolor();
        rayColor.mul(2.0f*ldrscale*sunlightscale*godraysstrength);
        LOCALPARAM(godRayColor, rayColor);

        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        screenquad(vieww, viewh);

        bool reduced = targetWidth < vieww || targetHeight < viewh;
        if(godraysblur > 0)
        {
            int blurRadius = clamp(godraysblur, 1, MAXBLURRADIUS);
            if(ensureBilateralShaders(useMsaa, blurRadius))
            {
                glActiveTexture_(GL_TEXTURE1);
                if(useMsaa) glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, msdepthtex);
                else glBindTexture(GL_TEXTURE_RECTANGLE, gdepthtex);
                glActiveTexture_(GL_TEXTURE0);

                float invScaleRoot = 1.0f/sqrtf(max(godraysscale, 0.0625f));
                float depthSigma = (0.75f + 0.5f*blurRadius)*invScaleRoot;
                int blurShaderIndex = useMsaa ? 1 : 0;
                loopi(2)
                {
                    glBindFramebuffer_(GL_FRAMEBUFFER, godRayFbo[(i+1)%2]);
                    glViewport(0, 0, targetWidth, targetHeight);
                    godRayBilateralShader[blurShaderIndex][i]->set();
                    LOCALPARAMF(godRayScreen, float(vieww)/targetWidth, float(viewh)/targetHeight, float(targetWidth)/vieww, float(targetHeight)/viewh);
                    LOCALPARAMF(godRayBilateralSigma, depthSigma);
                    glBindTexture(GL_TEXTURE_RECTANGLE, godRayTex[i%2]);
                    screenquad(targetWidth, targetHeight);
                }
            }
            else
            {
                float blurweights[MAXBLURRADIUS+1], bluroffsets[MAXBLURRADIUS+1];
                setupblurkernel(blurRadius, blurweights, bluroffsets);
                loopi(2)
                {
                    glBindFramebuffer_(GL_FRAMEBUFFER, godRayFbo[(i+1)%2]);
                    glViewport(0, 0, targetWidth, targetHeight);
                    setblurshader(i%2, 1, blurRadius, blurweights, bluroffsets, GL_TEXTURE_RECTANGLE);
                    glBindTexture(GL_TEXTURE_RECTANGLE, godRayTex[i%2]);
                    screenquad(targetWidth, targetHeight);
                }
            }
        }

        GLuint rayCompositeTex = godRayTex[0];
        float upsampleSigma = 0.75f/sqrtf(max(godraysscale, 0.0625f));
        float blurBlend = clamp(godraysblur/3.0f, 0.0f, 1.0f);

        glBindFramebuffer_(GL_FRAMEBUFFER, useMsaa ? mshdrfbo : hdrfbo);
        glViewport(0, 0, vieww, viewh);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);

        if(reduced)
        {
            Shader *upsampleShader = ensureUpsampleShader(useMsaa);
            if(upsampleShader && upsampleShader != nullshader)
            {
                glActiveTexture_(GL_TEXTURE1);
                if(useMsaa) glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, msdepthtex);
                else glBindTexture(GL_TEXTURE_RECTANGLE, gdepthtex);
                glActiveTexture_(GL_TEXTURE0);

                upsampleShader->set();
                LOCALPARAMF(godRayScreen, float(vieww)/targetWidth, float(viewh)/targetHeight, float(targetWidth)/vieww, float(targetHeight)/viewh);
                LOCALPARAMF(godRayUpsampleParams, upsampleSigma, blurBlend);
                glBindTexture(GL_TEXTURE_RECTANGLE, rayCompositeTex);
                screenquad();
            }
            else
            {
                SETSHADER(scalelinear);
                glBindTexture(GL_TEXTURE_RECTANGLE, rayCompositeTex);
                screenquad(targetWidth, targetHeight);
            }
        }
        else
        {
            SETSHADER(scalelinear);
            glBindTexture(GL_TEXTURE_RECTANGLE, rayCompositeTex);
            screenquad(targetWidth, targetHeight);
        }
        glDisable(GL_BLEND);

        if(hadBlend) glEnable(GL_BLEND);
        if(hadDepthTest) glEnable(GL_DEPTH_TEST);
        if(hadScissor) glEnable(GL_SCISSOR_TEST);
    }

    void cleanup()
    {
        loopi(2) if(godRayFbo[i]) { glDeleteFramebuffers_(1, &godRayFbo[i]); godRayFbo[i] = 0; }
        loopi(2) if(godRayTex[i]) { glDeleteTextures(1, &godRayTex[i]); godRayTex[i] = 0; }
        targetWidth = targetHeight = -1;

        godRayShader = NULL;
        loopi(2) godRayUpsampleShader[i] = NULL;
        loopi(2) loopj(2) godRayBilateralShader[i][j] = NULL;
        loopi(2) godRayBilateralKey[i] = -1;
        shaderVariantKey = -1;
    }
}
