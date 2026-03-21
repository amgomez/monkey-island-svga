    /*
        CRT-interlaced-halation shader - Monkey HD pass2

        Derived from crt-interlaced-halation-pass2.glsl.
        Monkey HD renders from a true 640x400 source, so this variant keeps
        the field phase stable and avoids collapsing pairs of lines into a
        simulated 200-line source.
    */
#ifdef GL_ES
precision highp float;
#endif

            #define LINEAR_PROCESSING
            #define CURVATURE
            #define USEGAUSSIAN
            // Keep the glow contribution from previous passes.
            #define MULTIPASS

            #define FIX(c) max(abs(c), 1e-5);
            #define PI 3.141592653589

            #ifdef LINEAR_PROCESSING
            #       define TEX2D(c) pow(COMPAT_TEXTURE(OrigTexture, (c)), vec4(CRTgamma))
            #else
            #       define TEX2D(c) COMPAT_TEXTURE(OrigTexture, (c))
            #endif

                    float CRTgamma = 2.4;
                    float monitorgamma = 2.2;
                    vec2 overscan = vec2(1.0,1.0);
                    vec2 aspect = vec2(1.0, 0.75);
                    float d = 2.0;
                    float R = 2.0;
                    const vec2 angle = vec2(0.0,0.0);
                    float cornersize = 0.01;
                    float cornersmooth = 800.0;

            float intersect(vec2 xy, vec2 sinangle, vec2 cosangle)
            {
                    float A = dot(xy,xy)+d*d;
                    float B = 2.0*(R*(dot(xy,sinangle)-d*cosangle.x*cosangle.y)-d*d);
                    float C = d*d + 2.0*R*d*cosangle.x*cosangle.y;
                    return (-B-sqrt(B*B-4.0*A*C))/(2.0*A);
            }

            vec2 bkwtrans(vec2 xy, vec2 sinangle, vec2 cosangle)
            {
                    float c = intersect(xy, sinangle, cosangle);
                    vec2 point = vec2(c)*xy;
                    point -= vec2(-R)*sinangle;
                    point /= vec2(R);
                    vec2 tang = sinangle/cosangle;
                    vec2 poc = point/cosangle;
                    float A = dot(tang,tang)+1.0;
                    float B = -2.0*dot(poc,tang);
                    float C = dot(poc,poc)-1.0;
                    float a = (-B+sqrt(B*B-4.0*A*C))/(2.0*A);
                    vec2 uv = (point-a*sinangle)/cosangle;
                    float r = FIX(R*acos(a));
                    return uv*r/sin(r/R);
            }

            vec2 fwtrans(vec2 uv, vec2 sinangle, vec2 cosangle)
            {
                    float r = FIX(sqrt(dot(uv,uv)));
                    uv *= sin(r/R)/r;
                    float x = 1.0-cos(r/R);
                    float D = d/R + x*cosangle.x*cosangle.y+dot(uv,sinangle);
                    return d*(uv*cosangle-x*sinangle)/D;
            }

            vec3 maxscale(vec2 sinangle, vec2 cosangle)
            {
                    vec2 c = bkwtrans(-R * sinangle / (1.0 + R/d*cosangle.x*cosangle.y), sinangle, cosangle);
                    vec2 a = vec2(0.5,0.5)*aspect;
                    vec2 lo = vec2(fwtrans(vec2(-a.x,c.y), sinangle, cosangle).x,
                                 fwtrans(vec2(c.x,-a.y), sinangle, cosangle).y)/aspect;
                    vec2 hi = vec2(fwtrans(vec2(+a.x,c.y), sinangle, cosangle).x,
                                 fwtrans(vec2(c.x,+a.y), sinangle, cosangle).y)/aspect;
                    return vec3((hi+lo)*aspect*0.5,max(hi.x-lo.x,hi.y-lo.y));
            }

            vec4 scanlineWeights(float distance, vec4 color)
            {
            #ifdef USEGAUSSIAN
                    vec4 wid = 0.3 + 0.1 * pow(color, vec4(3.0));
                    vec4 weights = vec4(distance / wid);
                    return 0.4 * exp(-weights * weights) / wid;
            #else
                    vec4 wid = 2.0 + 2.0 * pow(color, vec4(4.0));
                    vec4 weights = vec4(distance / 0.3);
                    return 1.4 * exp(-pow(weights * rsqrt(0.5 * wid), wid)) / (0.6 + 0.2 * wid);
            #endif
            }

#if defined(VERTEX)

#if __VERSION__ >= 130
#define COMPAT_VARYING out
#define COMPAT_ATTRIBUTE in
#define COMPAT_TEXTURE texture
#else
#define COMPAT_VARYING varying
#define COMPAT_ATTRIBUTE attribute
#define COMPAT_TEXTURE texture2D
#endif

#ifdef GL_ES
#define COMPAT_PRECISION mediump
#else
#define COMPAT_PRECISION
#endif

COMPAT_ATTRIBUTE vec4 VertexCoord;
COMPAT_ATTRIBUTE vec4 COLOR;
COMPAT_ATTRIBUTE vec4 TexCoord;
COMPAT_VARYING vec4 COL0;
COMPAT_VARYING vec4 TEX0;
COMPAT_VARYING vec2 one;
COMPAT_VARYING float mod_factor;
COMPAT_VARYING vec2 ilfac;
COMPAT_VARYING vec3 stretch;
COMPAT_VARYING vec2 sinangle;
COMPAT_VARYING vec2 cosangle;

uniform mat4 MVPMatrix;
uniform COMPAT_PRECISION int FrameDirection;
uniform COMPAT_PRECISION int FrameCount;
uniform COMPAT_PRECISION vec2 OutputSize;
uniform COMPAT_PRECISION vec2 TextureSize;
uniform COMPAT_PRECISION vec2 InputSize;
uniform COMPAT_PRECISION vec2 OrigInputSize;
uniform COMPAT_PRECISION vec2 OrigTextureSize;

#define vTexCoord TEX0.xy
#define SourceSize vec4(TextureSize, 1.0 / TextureSize)
#define outsize vec4(OutputSize, 1.0 / OutputSize)

void main()
{
    gl_Position = MVPMatrix * VertexCoord;
    COL0 = COLOR;
    TEX0.xy = TexCoord.xy;
    sinangle = sin(angle);
    cosangle = cos(angle);
    stretch = maxscale(sinangle, cosangle);

    // Monkey HD input is already a real 640x400 image. Keep the interlaced
    // two-line field structure, but sample adjacent physical lines so the
    // visible scanlines stay one source line thick instead of collapsing into
    // 200-line pairs.
    ilfac = vec2(1.0, 2.0);
    one = vec2(1.0 / OrigTextureSize.x, 1.0 / OrigTextureSize.y);
    mod_factor = vTexCoord.x * OrigTextureSize.x * outsize.x / OrigInputSize.x;
}

#elif defined(FRAGMENT)

#if __VERSION__ >= 130
#define COMPAT_VARYING in
#define COMPAT_TEXTURE texture
out vec4 FragColor;
#else
#define COMPAT_VARYING varying
#define FragColor gl_FragColor
#define COMPAT_TEXTURE texture2D
#endif

#ifdef GL_ES
#define COMPAT_PRECISION mediump
#else
#define COMPAT_PRECISION
#endif

uniform COMPAT_PRECISION int FrameDirection;
uniform COMPAT_PRECISION int FrameCount;
uniform COMPAT_PRECISION vec2 OutputSize;
uniform COMPAT_PRECISION vec2 TextureSize;
uniform COMPAT_PRECISION vec2 InputSize;
uniform COMPAT_PRECISION vec2 OrigInputSize;
uniform COMPAT_PRECISION vec2 OrigTextureSize;
uniform sampler2D Texture;
uniform sampler2D OrigTexture;
COMPAT_VARYING vec4 TEX0;
COMPAT_VARYING vec2 one;
COMPAT_VARYING float mod_factor;
COMPAT_VARYING vec2 ilfac;
COMPAT_VARYING vec3 stretch;
COMPAT_VARYING vec2 sinangle;
COMPAT_VARYING vec2 cosangle;

#define Source Texture
#define vTexCoord TEX0.xy
#define SourceSize vec4(TextureSize, 1.0 / TextureSize)
#define outsize vec4(OutputSize, 1.0 / OutputSize)

void main()
{
                    vec2 xy = vTexCoord;
#ifdef CURVATURE
                    vec2 cd = vTexCoord;
                    cd *= OrigTextureSize / OrigInputSize;
                    cd = (cd - vec2(0.5)) * aspect * stretch.z + stretch.xy;
                    xy = (bkwtrans(cd, sinangle, cosangle) / overscan / aspect + vec2(0.5)) * OrigInputSize / OrigTextureSize;
#endif
                    vec2 cd2 = xy;
                    cd2 *= OrigTextureSize / OrigInputSize;
                    cd2 = (cd2 - vec2(0.5)) * overscan + vec2(0.5);
                    cd2 = min(cd2, vec2(1.0) - cd2) * aspect;
                    vec2 cdist = vec2(cornersize);
                    cd2 = (cdist - min(cd2, cdist));
                    float dist = sqrt(dot(cd2, cd2));
                    float cval = clamp((cdist.x - dist) * cornersmooth, 0.0, 1.0);

                    vec2 xy2 = ((xy * OrigTextureSize / OrigInputSize - vec2(0.5)) + vec2(0.5)) * InputSize / TextureSize;
                    vec2 ilfloat = vec2(0.0, 0.0);
                    vec2 ratio_scale = (xy * SourceSize.xy - vec2(0.5) + ilfloat) / ilfac;
                    vec2 uv_ratio = fract(ratio_scale);
                    xy = (floor(ratio_scale) * ilfac + vec2(0.5) - ilfloat) / SourceSize.xy;

                    vec4 coeffs = PI * vec4(1.0 + uv_ratio.x, uv_ratio.x, 1.0 - uv_ratio.x, 2.0 - uv_ratio.x);
                    coeffs = FIX(coeffs);
                    coeffs = 2.0 * sin(coeffs) * sin(coeffs / 2.0) / (coeffs * coeffs);
                    coeffs /= dot(coeffs, vec4(1.0));

                    vec4 col = clamp(mat4(
                        TEX2D(xy + vec2(-one.x, 0.0)),
                        TEX2D(xy),
                        TEX2D(xy + vec2(one.x, 0.0)),
                        TEX2D(xy + vec2(2.0 * one.x, 0.0))) * coeffs,
                        0.0, 1.0);
                    vec4 col2 = clamp(mat4(
                        TEX2D(xy + vec2(-one.x, one.y)),
                        TEX2D(xy + vec2(0.0, one.y)),
                        TEX2D(xy + one),
                        TEX2D(xy + vec2(2.0 * one.x, one.y))) * coeffs,
                        0.0, 1.0);

#ifndef LINEAR_PROCESSING
                    col = pow(col, vec4(CRTgamma));
                    col2 = pow(col2, vec4(CRTgamma));
#endif

                    vec4 weights = scanlineWeights(uv_ratio.y, col);
                    vec4 weights2 = scanlineWeights(1.0 - uv_ratio.y, col2);
                    vec3 mul_res = (col * weights + col2 * weights2).rgb;
#ifdef MULTIPASS
                    mul_res += pow(COMPAT_TEXTURE(Source, xy2).rgb, vec3(monitorgamma)) * 0.1;
#endif
                    mul_res *= vec3(cval);
                    mul_res = pow(mul_res, vec3(1.0 / monitorgamma));
                    FragColor = vec4(mul_res, 1.0);
}
#endif
