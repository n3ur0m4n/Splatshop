// structs and constants that are used on host and device

#pragma once

#include "./MouseEvents.h"

// #define QUATERNIONS_FLOAT
// #define QUATERNIONS_HALF

// #define COLORS_FLOAT
// #define COLORS_UINT16

// #if defined(COLORS_FLOAT)
// 	typedef float COLTYPE;
// #elif defined(COLORS_UINT16)
// 	typedef uint16_t COLTYPE;
// #endif

using glm::vec2;
using glm::vec3;
using glm::vec4;
using glm::ivec2;
using glm::ivec3;
using glm::ivec4;
using glm::mat4;

constexpr float MAX_SCREENSPACE_SPLATSIZE = 900.0f;

constexpr int SPLATAPPEARANCE_GAUSSIAN = 0;
constexpr int SPLATAPPEARANCE_POINT = 1;

constexpr int MOD_SHIFT = 1;
constexpr int MOD_CTRL = 2;
constexpr int MOD_ALT = 4;

// constexpr float TILE_SIZE = 16.0f;
// constexpr int TILE_SIZE_INT = 16;
// constexpr float TILE_SIZE = 8.0f; // TODO: much faster with insertion sort
// constexpr int TILE_SIZE_INT = 8; // TODO: much faster with insertion sort
constexpr int TILE_SIZE_3DGS = 16;
constexpr int TILE_SIZE_PERSPCORRECT = 8;

constexpr int RENDERMODE_COLOR = 0;
constexpr int RENDERMODE_DEPTH = 1;
constexpr int RENDERMODE_TILES = 2;
constexpr int RENDERMODE_HEATMAP = 3;
constexpr int RENDERMODE_PRIMITIVES = 4;
constexpr int RENDERMODE_COLOR_ASYNCMEM = 100;

constexpr int INTERSECTION_APPROXIMATE = 0;
constexpr int INTERSECTION_3DGS = 1;
constexpr int INTERSECTION_TIGHTBB = 2;

constexpr int BRUSHCOLORMODE_NORMAL = 0;
constexpr int BRUSHCOLORMODE_HUE_SATURATION = 1;

constexpr int SPLATRENDERER_3DGS = 0;
constexpr int SPLATRENDERER_PERSPECTIVE_CORRECT = 1;

constexpr uint32_t FLAGS_SELECTED             = 1 <<  0;
constexpr uint32_t FLAGS_HIGHLIGHTED          = 1 <<  1;
constexpr uint32_t FLAGS_HIGHLIGHTED_NEGATIVE = 1 <<  2;
constexpr uint32_t FLAGS_DELETED              = 1 <<  3;
constexpr uint32_t FLAGS_DIRTY                = 1 <<  4;
constexpr uint32_t FLAGS_DISABLE_DEPTHWRITE   = 1 << 16;

// struct half2{
// 	__half x;
// 	__half y;
// };

//struct half4{
//	__half r;
//	__half g;
//	__half b;
//	__half a;
//};

constexpr int ACTION_NONE = 0;
constexpr int ACTION_BRUSHING = 1;
constexpr int ACTION_PLACING = 2;

struct Color{
	uint16_t r;
	uint16_t g;
	uint16_t b;
	uint16_t a;

	// Following functions are provided for use so that when we change from u16 to u8 or float,
	// we don't have to change color conversions in multiple places.
	static float normalize(uint16_t value){
		return float(value) / 65536.0f;
	}

	static float denormalize(float value){
		return clamp(value * 65536.0f, 0.0f, 65535.0f);
	}

	vec4 normalized(){
		return vec4{
			Color::normalize(r),
			Color::normalize(g),
			Color::normalize(b),
			Color::normalize(a),
		};
	}

	static Color fromNormalized(vec4 color){
		Color c;
		c.r = denormalize(color.r);
		c.g = denormalize(color.g);
		c.b = denormalize(color.b);
		c.a = denormalize(color.a);

		return c;
	}
};

enum class BRUSHMODE{
	NONE         = 0,
	NORMAL       = 1,
	ERASE        = 2,
	ADD          = 3,
	MULTIPLY     = 4,
	SELECT       = 5,
	REMOVE_FLAGS = 6,
};

constexpr uint32_t BRUSH_INTERSECTION_CENTER = 0;
constexpr uint32_t BRUSH_INTERSECTION_BORDER = 1;

struct Brush{
	BRUSHMODE mode            = BRUSHMODE::NONE;
	float size                = 100.0f;
	float opacity             = 0.5f;
	float minSplatSize        = 0.0f;
	bool active               = false;
	uint32_t intersectionmode = BRUSH_INTERSECTION_BORDER;
	vec4 color                = {1.0f, 0.0f, 0.0f, 1.0f};
};

struct RectSelect{
	vec2 start;
	vec2 end;
	bool active = false;
	bool startpos_specified = false;
	bool unselecting = false;
};

struct ColorCorrection{
	float brightness = 0.0f;
	float contrast = 0.0f;
	float gamma = 1.0f;

	float hue = 0.0f;
	float saturation = 0.0f;
	float lightness = 0.0f;
};

// constexpr int STAGEDATA_BITS = 24;

// #define STAGEDATA_16BYTE
// #define STAGEDATA_20BYTE
#define STAGEDATA_24BYTE

// #define FRAGWISE_ORDERING

#if defined(STAGEDATA_16BYTE)
	struct StageData{
		glm::i16vec2 basisvector1_encoded;
		glm::i16vec2 imgPos_encoded;
		uint32_t color;
		// uint32_t flags;
		int16_t basisvector2_encoded;
		int16_t depth_encoded;
	};
#elif defined(STAGEDATA_20BYTE)
	struct StageData{
		glm::i16vec2 basisvector1_encoded;
		glm::i16vec2 basisvector2_encoded;
		glm::i16vec2 imgPos_encoded;
		uint32_t color;
		// uint32_t flags;
		 float depth;
	};
#elif defined(STAGEDATA_24BYTE)
struct StageData {
	glm::i16vec2 basisvector1_encoded;
	glm::i16vec2 basisvector2_encoded;
	glm::i16vec2 imgPos_encoded;
	uint32_t color;
	uint32_t flags;
	float depth;

	// // Padding to 64 byte
	// float padding[10];

	// // Padding to 80 byte
	// float padding[14];
};
#endif
struct StageData_perspectivecorrect {
	glm::vec4 VPMT1;
	glm::vec4 VPMT2;
	glm::vec4 VPMT4;
	glm::vec4 MT3;
	uint32_t color;
	uint32_t flags;
};


// glm::i16vec2 encode_basisvector_i16vec2(vec2 basisvector){

// 	float length = glm::length(basisvector);
// 	float angle = atan2(basisvector.y, basisvector.x);

// 	int16_t ilength = length * 100.0f;
// 	int16_t iangle = angle * 10'000.0f;

// 	return {iangle, ilength};
// }

// vec2 decode_basisvector_i16vec2(glm::i16vec2 encoded){

// 	float length = float(encoded.y) / 100.0f;
// 	float angle = float(encoded.x) / 10'000.0f;

// 	float x = cos(angle);
// 	float y = sin(angle);

// 	return vec2{x, y} * length;
// }

struct Pixel{
	uint32_t color;
	float depth;
};

struct Rectangle{
	float x;
	float y;
	float width;
	float height;
};

struct Box3 {
	vec3 min = { Infinity, Infinity, Infinity };
	vec3 max = { -Infinity, -Infinity, -Infinity };

	bool isDefault() {
		return min.x == Infinity && min.y == Infinity && min.z == Infinity && max.x == -Infinity && max.y == -Infinity && max.z == -Infinity;
	}

	bool isEqual(Box3 box, float epsilon) {
		float diff_min = length(box.min - min);
		float diff_max = length(box.max - max);

		if (diff_min >= epsilon) return false;
		if (diff_max >= epsilon) return false;

		return true;
	}
};

struct Texture{
	int width = 0;
	int height = 0;
	uint8_t* data = nullptr;              // regular CUdeviceptr
	cudaSurfaceObject_t surface = -1;     // Cuda-mapping of an OpenGL texture/image
	cudaTextureObject_t cutexture = -1;   // Cuda-mapping of an OpenGL texture
};

struct TriangleData{
	uint32_t count = 0;
	bool visible = true;
	bool locked = false;
	mat4 transform = mat4(1.0f);

	vec3 min;
	vec3 max;

	vec3* position    = nullptr;
	vec2* uv          = nullptr;
	uint32_t* colors  = nullptr;
	uint32_t* indices = nullptr;
};

struct Splat{
	vec3 position;
	vec3 scale;
	vec4 quaternion;
	vec4 color;
	uint32_t flags;
};

struct MagicWandArgs{
	vec4 baseColor;
	float tolerance;
	vec3 startPosition;
	Splat* seeds[2];
	uint32_t* numSeeds[2];
	uint32_t* iteration;
};

constexpr int MATERIAL_MODE_COLOR          = 0;
constexpr int MATERIAL_MODE_VERTEXCOLOR    = 1;
constexpr int MATERIAL_MODE_UVS            = 2;
constexpr int MATERIAL_MODE_POSITION       = 3;
constexpr int MATERIAL_MODE_TEXTURED       = 4;

struct TriangleMaterial{
	vec4 color = vec4{1.0f, 0.0f, 0.0f, 1.0f};
	int mode = MATERIAL_MODE_COLOR;
	Texture texture;
	vec2 uv_offset = {0.0f, 0.0f};
	vec2 uv_scale = {1.0f, 1.0f};
};

struct TriangleModelQueue{
	uint32_t count;
	TriangleData* geometries;
	TriangleMaterial* materials;
};

// Selection volumes (box / sphere / cylinder), see actions/VolumeSelectAction.h
// The unit shapes are: box [-1, 1]^3, sphere of radius 1, cylinder along z with radius 1 and z in [-1, 1].
// <volumeToWorld> transforms the unit shape into the world.
constexpr int VOLUME_SHAPE_BOX      = 0;
constexpr int VOLUME_SHAPE_SPHERE   = 1;
constexpr int VOLUME_SHAPE_CYLINDER = 2;

constexpr int VOLUME_OP_PREVIEW        = 0;
constexpr int VOLUME_OP_SELECT_ADD     = 1;
constexpr int VOLUME_OP_SELECT_REMOVE  = 2;
constexpr int VOLUME_OP_SELECT_REPLACE = 3;
constexpr int VOLUME_OP_DELETE         = 4;

struct VolumeGPU{
	mat4 worldToVolume;
	int shape;
	uint32_t color;       // ABGR
	float fillAlpha;      // opacity of each visible face
};

inline bool isInsideUnitVolume(int shape, vec3 p){
	if(shape == VOLUME_SHAPE_BOX){
		return fabsf(p.x) <= 1.0f && fabsf(p.y) <= 1.0f && fabsf(p.z) <= 1.0f;
	}else if(shape == VOLUME_SHAPE_SPHERE){
		return glm::dot(p, p) <= 1.0f;
	}else if(shape == VOLUME_SHAPE_CYLINDER){
		return (p.x * p.x + p.y * p.y) <= 1.0f && fabsf(p.z) <= 1.0f;
	}
	return false;
}

// Intersects the ray o + t * d with the unit shape. Returns true and the interval [t0, t1] if hit.
inline bool intersectUnitVolume(int shape, vec3 o, vec3 d, float& t0, float& t1){

	auto slab = [](float o, float d, float& tmin, float& tmax) -> bool {
		if(fabsf(d) < 1e-12f){
			return fabsf(o) <= 1.0f;
		}
		float ta = (-1.0f - o) / d;
		float tb = ( 1.0f - o) / d;
		if(ta > tb){ float tmp = ta; ta = tb; tb = tmp; }
		tmin = fmaxf(tmin, ta);
		tmax = fminf(tmax, tb);
		return tmin <= tmax;
	};

	float tmin = -1e30f;
	float tmax =  1e30f;

	if(shape == VOLUME_SHAPE_BOX){
		if(!slab(o.x, d.x, tmin, tmax)) return false;
		if(!slab(o.y, d.y, tmin, tmax)) return false;
		if(!slab(o.z, d.z, tmin, tmax)) return false;
	}else if(shape == VOLUME_SHAPE_SPHERE){
		float a = glm::dot(d, d);
		float b = glm::dot(o, d);
		float c = glm::dot(o, o) - 1.0f;
		float disc = b * b - a * c;
		if(disc < 0.0f || a < 1e-20f) return false;
		float s = sqrtf(disc);
		tmin = (-b - s) / a;
		tmax = (-b + s) / a;
	}else if(shape == VOLUME_SHAPE_CYLINDER){
		float a = d.x * d.x + d.y * d.y;
		float b = o.x * d.x + o.y * d.y;
		float c = o.x * o.x + o.y * o.y - 1.0f;
		if(a < 1e-20f){
			if(c > 0.0f) return false;
		}else{
			float disc = b * b - a * c;
			if(disc < 0.0f) return false;
			float s = sqrtf(disc);
			tmin = (-b - s) / a;
			tmax = (-b + s) / a;
		}
		if(!slab(o.z, d.z, tmin, tmax)) return false;
	}else{
		return false;
	}

	if(tmax < 0.0f) return false;

	t0 = tmin;
	t1 = tmax;

	return true;
}

struct Line{
	vec3 start;
	vec3 end;
	uint32_t color;
};

struct LineData{
	uint32_t count = 0;

	Line* lines;
};

struct RemainingOpacity{
	float opacity;
	uint32_t contributions;
};


struct PointData{
	uint32_t count = 0;
	uint32_t numUploaded = 0;
	bool visible = true;
	bool locked = false;

	mat4 transform = mat4(1.0f);

	vec3 min;
	vec3 max;

	vec3* position = nullptr;
	uint32_t* color = nullptr;
	uint32_t* flags = nullptr;
};

// see https://github.com/mkkellogg/GaussianSplats3D
// LICENSE: MIT
struct Cov3DElements{
	float m11;
	float m12;
	float m13;
	float m22;
	float m23;
	float m33;
};

struct GaussianData{

	uint32_t count = 0;
	uint32_t numUploaded = 0;
	bool visible = true;
	bool locked = false;
	bool writeDepth = true;
	int shDegree = 0;
	int numSHCoefficients = 0;

	mat4 transform = mat4(1.0f);

	vec3 min;
	vec3 max;

	// Splat Attributes
	vec3* position             = nullptr;
	vec3* scale                = nullptr;
	vec4* quaternion           = nullptr;
	Color* color               = nullptr;
	uint32_t* color_resolved   = nullptr;
	float* sphericalHarmonics  = nullptr;

	Cov3DElements* cov3d       = nullptr;

	// auxilliary attributes, used for rendering and modification
	float* depth               = nullptr;
	uint32_t* flags            = nullptr;
	// StageData* basisvectorsNstuff = nullptr;
};

constexpr int COLORMODE_TEXTURE          = 0;
constexpr int COLORMODE_UV               = 1;
constexpr int COLORMODE_TRIANGLE_ID      = 2;
constexpr int COLORMODE_TIME             = 3;
constexpr int COLORMODE_TIME_NORMALIZED  = 4;

constexpr int SAMPLEMODE_NEAREST     = 0;
constexpr int SAMPLEMODE_LINEAR      = 1;

struct Tile{
	uint32_t firstIndex;
	uint32_t lastIndex;
	uint32_t X;
	uint32_t Y;
	uint32_t tileSize; // standard 16x16, maybe 8x8
	uint32_t subindex; // just 0 if 16x16, [0, 3] if 8x8
};

struct TileSubset{
	uint16_t tile_x;
	uint16_t tile_y;
	uint32_t firstIndex;
	uint32_t lastIndex;
	uint32_t subsetIndex;
	uint32_t numSubsets;
	uint64_t* subsetsFramebuffer;
	bool isFinished;
	
	TileSubset* next;
};

struct ControllerState{
	uint32_t packetNum;
	uint64_t buttonPressedMask;
	uint64_t buttonTouchedMask;
};

constexpr uint32_t PROFILE_BINS = 100;
struct DbgProfileUtilization{
	uint64_t startBin;
	uint32_t numSplatsRendered[PROFILE_BINS];
};


struct DbgProfileTime{
	uint64_t t_start;
	uint64_t t_end;
};

struct DeviceState{
	int counter;

	uint32_t visibleSplats;
	uint32_t visibleSplatFragments;
	uint32_t numSelectedSplats;
	uint32_t numDeletedSplats;
	uint32_t numSplatsInLargestTile;

	vec3 hovered_pos;
	uint32_t hovered_primitive;
	float hovered_depth;
	vec3 mouseTargetIntersection;
	vec4 hovered_color;

	struct {
		float radius;
		glm::mat4 view;
	} viewdata;

	struct {
		uint32_t numAccepted;
		uint32_t numRejected;
	} dbg;
};

struct RenderTarget{
	uint64_t* framebuffer = nullptr;
	uint64_t* indexbuffer = nullptr;
	int width;
	int height;
	mat4 view;
	mat4 proj;
	mat4 VP;
	bool isLeft = false;
	bool isRight = false;
};

struct Uniforms{
	float time;
	float pad;
	mat4 world;
	mat4 camWorld;
	mat4 transform;
	int colorMode;
	int sampleMode;
	uint32_t frameCount;
	// uint32_t numSelectedSplats;
	int viewmode;
	int showSolid;
	int showTiles;
	int showRing;
	int makePoints;
	int rendermode;
	int brushColorMode;
	int intersectionMode;
	uint32_t fragmentCounter;
	float splatSize;
	bool disableFrustumCulling;
	bool cullSmallSplats;

	struct {
		bool show;
		ivec2 start;
		ivec2 size;
	} inset;


	int vrEnabled;
	int measure;
	int frontToBack;
	int sortEnabled;
};

struct Keys{
	int mods;

	uint32_t keyStates[65536];

	bool isCtrlDown(){
		return keyStates[341] != 0;
		// return true;
	}

	bool isAltDown(){
		return keyStates[342] != 0;
	}

	bool isShiftDown(){
		return keyStates[340] != 0;
	}
};

struct KeyEvents{

	struct KeyEvent{
		uint32_t key;
		uint32_t action;
		uint32_t mods;
	};

	int numEvents;
	KeyEvent events[8];
};

struct VrController{
	mat4 pose;
	uint64_t buttonPressed;
	uint64_t buttonTouched;
	vec2 axis[5];
	bool valid;
};

struct SceneNodeD {
	glm::mat4 transform;
	char name[64];
	uint32_t color;
	uint32_t pad_0;
	uint32_t pad_1;
	uint32_t pad_2;
};

//inline void print(glm::mat4 m){
//	auto r0 = glm::row(m, 0);
//	auto r1 = glm::row(m, 1);
//	auto r2 = glm::row(m, 2);
//	auto r3 = glm::row(m, 3);
//
//	printf("%10.6f, %10.6f, %10.6f, %10.6f \n", r0.x, r0.y, r0.z, r0.w);
//	printf("%10.6f, %10.6f, %10.6f, %10.6f \n", r1.x, r1.y, r1.z, r1.w);
//	printf("%10.6f, %10.6f, %10.6f, %10.6f \n", r2.x, r2.y, r2.z, r2.w);
//	printf("%10.6f, %10.6f, %10.6f, %10.6f \n", r3.x, r3.y, r3.z, r3.w);
//}

inline void print(glm::vec3 v){
	printf("%7.3f, %7.3f, %7.3f \n", v.x, v.y, v.z);
}

inline void print(glm::vec4 v){
	printf("%7.3f, %7.3f, %7.3f, %7.3f \n", v.x, v.y, v.z, v.w);
}


struct CommonLaunchArgs{
	Uniforms uniforms;
	DeviceState* state;
	
	MouseEvents mouseEvents;
	MouseEvents mouseEvents_prev;
	Keys* keys;
	Brush brush;
	RectSelect rectselect;
	KeyEvents keyEvents;

	// uint32_t* indices;
	// uint32_t* indices_unsorted;
	// uint32_t* depths_tiledwise;
	// uint32_t* tileIDs;
	// uint32_t* tileIDs_depthsorted;
	// uint32_t* ordering;
	// vec2* basisvectors;
	// vec2* basisvectors_unsorted;
	// Tile* tiles;
};


constexpr int FILTER_SELECTION_DONTCARE = 0;
constexpr int FILTER_SELECTION_SELECTED = 1;
constexpr int FILTER_SELECTION_UNSELECTED = 2;

constexpr int FILTER_DELETED_DONTCARE = 0;
constexpr int FILTER_DELETED_NONDELETED = 1;
constexpr int FILTER_DELETED_DELETED = 2;

struct FilterRules{
	int selection = FILTER_SELECTION_DONTCARE;
	int deleted = FILTER_DELETED_DONTCARE;
};

// see https://en.wikipedia.org/wiki/HSL_and_HSV
// https://en.wikipedia.org/wiki/HSL_and_HSV#From_RGB
// rgb in [0, 1]
inline vec3 rgbToHSL(float r, float g, float b){

	float M = max(max(r, g), b);
	float m = min(min(r, g), b);
	float C = M - m;                   // Chroma
	float V = M;
	float L = (M + m) / 2.0f;          // Lightness

	float Hh = 0.0f;
	// first compute H in [0, 6.0]
	if(M == r){
		Hh = fmodf((g - b) / C, 6.0f);
	}else if(M == g){
		Hh = (b - r) / C + 2.0f;
	}else if(M == b){
		Hh = (r - g) / C + 4.0f;
	}

	// Map to degrees [0, 360]
	float H = fmodf(60.0f * Hh + 360.0f, 360.0f);

	float Sv = V == 0.0f ? 0.0f : C / V;
	float Sl = V == 0.0f ? 0.0f : (V - L) / min(L, 1.0f - L);

	float hue = H;
	float saturation = Sl;
	float lightness = L;

	return vec3{hue, saturation, lightness};
}

inline vec3 hslToRgb(float H, float Sl, float L){
	float C = (1.0f - abs(2.0f * L - 1.0f)) * Sl;
	float Hh = H / 60.0f;
	float X = C * (1.0f - abs(fmodf(Hh, 2.0f) - 1.0f));

	vec3 rgb;

	if(0.0f <= Hh && Hh < 1.0f){
		rgb = vec3(C, X, 0.0f);
	}else if(1.0f <= Hh && Hh < 2.0f){
		rgb = vec3(X, C, 0.0f);
	}else if(2.0f <= Hh && Hh < 3.0f){
		rgb = vec3(0.0f, C, X);
	}else if(3.0f <= Hh && Hh < 4.0f){
		rgb = vec3(0.0f, X, C);
	}else if(4.0f <= Hh && Hh < 5.0f){
		rgb = vec3(X, 0.0f, C);
	}else if(5.0f <= Hh && Hh < 6.0f){
		rgb = vec3(C, 0.0f, X);
	}else{
		rgb = vec3(0.0f, 0.0f, 0.0f);
	}

	float m = L - C / 2.0f;

	rgb = rgb + m;

	return rgb;
}

inline vec4 applyColorCorrection(vec4 color, ColorCorrection colorCorrection){

	float r = color.r;
	float g = color.g;
	float b = color.b;

	// assume that brightness is given in the context of 8-bit, 0-255 colors.
	r = clamp(r + colorCorrection.brightness / 256.0f, 0.0f, 1.0f);
	g = clamp(g + colorCorrection.brightness / 256.0f, 0.0f, 1.0f);
	b = clamp(b + colorCorrection.brightness / 256.0f, 0.0f, 1.0f);

	// Contrast formula from: https://www.dfstudios.co.uk/articles/programming/image-programming-algorithms/image-processing-algorithms-part-5-contrast-adjustment/
	// assume that contrast is given in the context of 8-bit, 0-255 colors.
	float contrastFactor = (259.0f * (colorCorrection.contrast + 255.0f)) / (255.0f * (259.0f - colorCorrection.contrast));
	r = clamp(contrastFactor * (256.0f * r - 128.0f) + 128.0f, 0.0f, 255.0f) / 256.0f;
	g = clamp(contrastFactor * (256.0f * g - 128.0f) + 128.0f, 0.0f, 255.0f) / 256.0f;
	b = clamp(contrastFactor * (256.0f * b - 128.0f) + 128.0f, 0.0f, 255.0f) / 256.0f;

	// Gamma formula from: https://www.dfstudios.co.uk/articles/programming/image-programming-algorithms/image-processing-algorithms-part-6-gamma-correction/
	float gammaCorrection = 1.0f / colorCorrection.gamma;
	r = pow(r , gammaCorrection);
	g = pow(g , gammaCorrection);
	b = pow(b , gammaCorrection);

	// HSL see https://en.wikipedia.org/wiki/HSL_and_HSV#From_RGB
	vec3 hsl = rgbToHSL(r, g, b);
	hsl.x = fmodf(hsl.x + colorCorrection.hue + 360.0f, 360.0f);
	hsl.y = clamp(hsl.y + colorCorrection.saturation, 0.0f, 1.0f);
	hsl.z = clamp(hsl.z + colorCorrection.lightness, 0.0f, 1.0f);

	vec3 rgb = hslToRgb(hsl.x, hsl.y, hsl.z);

	vec4 corrected;
	corrected.r = rgb.r;
	corrected.g = rgb.g;
	corrected.b = rgb.b;
	corrected.a = color.a;

	return corrected;
}