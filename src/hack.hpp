#ifndef HACK_H_
#define HACK_H_

#include "hack_types.hpp"

#include <algorithm>
#include <alloca.h>
#include <limits.h>
#include <math.h>

template <typename VARY_TYPE>
struct __HACK_Scanline;

/**
 * Our rendering context
 */
struct HACK_Context
{
    int width, height;
};

/**
 * Output of a fragment shader, 
 * contains the color and z depth of our pixel
 */
struct HACK_pixel {
    HACK_Vec4 color;
};

/**
 * Output of a vertex shader
 * contains the vertex position and an associated varying object with our vertex
 */
template <typename VARY_TYPE>
struct HACK_vertex {
    HACK_Vec3 position;
    VARY_TYPE varying;
};

/**
 * Scanline representation
 * Holds left/right positions and associated varying objects
 */
template <typename VARY_TYPE>
struct HACK_Scanline {
    int leftX, rightX;
    float leftZ, rightZ;
    VARY_TYPE leftVarying, rightVarying;
};

/**
 * Rasterize a set of triangles
 * polygonAttributes - <ATTR_TYPE>[] - this array holds all of the per vertex information for all of our triangles
 * uniforms - <UNIF_TYPE> - this object holds things that are uniform to all vertices in all of our triangles
 * vertexCount - int - the number of vertices, should be the length of polygonAttributes
 * vertexShader - fn(const ATTR_TYPE & attribute, const UNIF_TYPE & uniform, HACK_vertex<VARY_TYPE> & output)
 *              - function that transforms the triangles' vertices and sets up the varying data for further pipeline steps
 *              - your shader function should populate the output parameter
 * fragmentShader - fn(const VARY_TYPE & varying, const UNIF_TYPE & uniform, HACK_pixel & output)
 *                - function that determines the color of a pixel based on varyings and uniforms
 *                - your shader function should populate the output parameter
 * scanlines - an array of scanlines that will be used internally for scan conversion
 *           - should be equal to the height of the raster
 *           - this seems superfluous but there's no memory allocation inside of hack so these must be supplied elsewhere
 */
template <typename ATTR_TYPE, typename VARY_TYPE, typename UNIF_TYPE>
inline void HACK_rasterize_triangles(const HACK_Context &ctx,
                    const ATTR_TYPE *polygonAttributes,
                    const UNIF_TYPE &uniforms,
                    const int vertexCount,
                    void (*vertexShader) (const ATTR_TYPE &attribute, const UNIF_TYPE &uniform, HACK_vertex<VARY_TYPE> &output),
                    void (*fragmentShader) (const VARY_TYPE &varying, const UNIF_TYPE &uniform, HACK_pixel &output),
                    HACK_Scanline<VARY_TYPE> *scanlines)
{
    
    // every three vertexes is a triangle we should rasterize
    for (int v = 0; v < vertexCount;) {
        __HACK_rasterize_triangle(ctx, v, polygonAttributes, uniforms, vertexShader, fragmentShader, scanlines);
        v += 3;
    }
}

/**
 * INTERNAL - Rasterize a single triangle
 * triangleId - int - the ID of the triangle we're rendering, this is used to calc which polygon attributes we will use for our vertexes
 * polygonAttributes - <ATTR_TYPE>[] -
 * uniforms - <UNIF_TYPE> -
 * vertexShader - fn
 * fragmentShader - fn
 */
template <typename ATTR_TYPE, typename VARY_TYPE, typename UNIF_TYPE>
inline void __HACK_rasterize_triangle(const HACK_Context &ctx,
                                    const int triangleId,
                                    const ATTR_TYPE *polygonAttributes,
                                    const UNIF_TYPE &uniforms,
                                    void (*vertexShader) (const ATTR_TYPE &attribute, const UNIF_TYPE &uniform, HACK_vertex<VARY_TYPE> &output),
                                    void (*fragmentShader) (const VARY_TYPE &varying, const UNIF_TYPE &uniform, HACK_pixel &output),
                                    HACK_Scanline<VARY_TYPE> *scanlines)
{
    // allocate 3 outputs, one for each vertex
    HACK_vertex<VARY_TYPE> vertexShaderOutput[3];
    vertexShader(polygonAttributes[triangleId], uniforms, vertexShaderOutput[0]);
    vertexShader(polygonAttributes[triangleId + 1], uniforms, vertexShaderOutput[1]);
    vertexShader(polygonAttributes[triangleId + 2], uniforms, vertexShaderOutput[2]);
    
    int halfHeight = ctx.height / 2;
    int halfWidth = ctx.width / 2;
    
    // calc polygon normal and short circuit if needed
    
    // calculate number of scanlines needed for triangle
    // TODO(karl): clip to ctx y coords
    int bottomScanY = ceil(std::min(std::min(vertexShaderOutput[0].position.y * halfHeight, vertexShaderOutput[1].position.y * halfHeight), vertexShaderOutput[2].position.y * halfHeight));
    int topScanY = ceil(std::max(std::max(vertexShaderOutput[0].position.y * halfHeight, vertexShaderOutput[1].position.y * halfHeight), vertexShaderOutput[2].position.y * halfHeight));
    int scanlineNum = topScanY - bottomScanY;
    
    for (int i = 0; i < scanlineNum; ++i) {
        scanlines[i].leftX = INT_MAX;
        scanlines[i].rightX = INT_MIN;
    }
    
    // populate scanlines with values
    // this is where actual scanline conversion is done
    // TODO(karl): actually do it
    for (int i = 0; i < 3; ++i) {
        HACK_vertex<VARY_TYPE> &v1 = vertexShaderOutput[i];
        HACK_vertex<VARY_TYPE> &v2 = (i == 2) ? vertexShaderOutput[0] : vertexShaderOutput[i + 1];
        
        if (v1.position.y > v2.position.y) {
            // if our y is decreasing instead of increasing we need to flip ordering
            std::swap(v1, v2);
        }
        
        HACK_Vec3 &v1Position = v1.position;
        HACK_Vec3 &v2Position = v2.position;
        
        float dy = (v2Position.y - v1Position.y) * halfHeight;
        float dx = (v2Position.x - v1Position.x) * halfWidth;
        int bottomY = ceil(v1Position.y * halfHeight);
        int topY = ceil(v2Position.y * halfHeight);
        
        if (dy == 0) {
            // we skip horizontal lines because they'll be filled by diagonals later
            // also horizontal lines will fill things with NaNs because of division by zero...
            continue;
        }
        
        if (dx == 0) {
            // have to do special case for vertical line
            int x = ceil(v2Position.x) * halfWidth;
            for (int y = bottomY; y <= topY; ++y) {
                HACK_Scanline<VARY_TYPE> &scanline = scanlines[y - bottomScanY];
                scanline.leftX = std::min(scanline.leftX, x);
                scanline.rightX = std::max(scanline.rightX, x);
                float lerpVal = (y - v1Position.y * halfHeight) / (v2Position.y * halfHeight - v1Position.y * halfHeight);
                lerp(v1.varying, v2.varying, lerpVal, scanline.leftVarying);
                lerp(v1.varying, v2.varying, lerpVal, scanline.rightVarying);
                lerp(v1Position.z, v2Position.z, lerpVal, scanline.leftZ);
                lerp(v1Position.z, v2Position.z, lerpVal, scanline.rightZ);
            }
        } else {
        
            // this is slow, should be optimized
            float gradient = dx / dy;
            
            for (int y = bottomY; y <= topY; ++y) {
                // line equation
                int x = ceil(v1Position.x * halfWidth + (y - v1Position.y * halfHeight) * gradient);
                
                HACK_Scanline<VARY_TYPE> &scanline = scanlines[y - bottomScanY];
                scanline.leftX = std::min(scanline.leftX, x);
                scanline.rightX = std::max(scanline.rightX, x);
                float lerpVal = (y - v1Position.y * halfHeight) / (v2Position.y * halfHeight - v1Position.y * halfHeight);
                if (x == scanline.leftX) {
                    lerp(v1.varying, v2.varying, lerpVal, scanline.leftVarying);
                    lerp(v1Position.z, v2Position.z, lerpVal, scanline.leftZ);
                }
                if (x == scanline.  rightX) {
                    lerp(v1.varying, v2.varying, lerpVal, scanline.rightVarying);
                    lerp(v1Position.z, v2Position.z, lerpVal, scanline.rightZ);
                }
            }
        }
    }
    

    // we have all of our scanlines setup, now just loop through shading each pixel in the scanline
    VARY_TYPE lerpedVarying;
    HACK_pixel pixelOutput;
    for (int i = 0; i < scanlineNum; ++i) {
        HACK_Scanline<VARY_TYPE> &scanline = scanlines[i];
        
        // clip scanline to ctx space
        // TODO(karl): check against ctx x coords
        
        for (int j = scanline.leftX; j <= scanline.rightX; ++j) {
            // lerp the left and right of the scanline into
            float lerpVal = static_cast<float>(j - scanline.leftX) / static_cast<float>(scanline.rightX - scanline.leftX);
            lerp<VARY_TYPE>(scanline.leftVarying, scanline.rightVarying, lerpVal, lerpedVarying);
            float pixelZ = -1;
            lerp(scanline.leftZ, scanline.rightZ, lerpVal, pixelZ);
            int pixelX = j + halfWidth;
            int pixelY = i + bottomScanY + halfHeight;
            
            //NSLog(@"shading pixel {%d, %d, %f}", pixelX, pixelY, pixelZ);
            fragmentShader(lerpedVarying, uniforms, pixelOutput);
            //NSLog(@"color is {%f, %f, %f, %f}", pixelOutput.color.r, pixelOutput.color.g, pixelOutput.color.b, pixelOutput.color.a);
            
            // update depth and color buffers with our rendering context
            
        }
    }
}


#endif
