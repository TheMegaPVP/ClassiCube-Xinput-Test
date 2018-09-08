#include "SelectionBox.h"
#include "ExtMath.h"
#include "GraphicsAPI.h"
#include "GraphicsCommon.h"
#include "Event.h"
#include "Funcs.h"
#include "Game.h"

/* Data for a selection box. */
struct SelectionBox {
	Vector3 Min, Max;
	PackedCol Col;
	Real32 MinDist, MaxDist;
};

static void SelectionBox_Render(struct SelectionBox* box, VertexP3fC4b** vertices, VertexP3fC4b** lineVertices) {
	Real32 offset = box->MinDist < 32.0f * 32.0f ? (1.0f / 32.0f) : (1.0f / 16.0f);
	Vector3 coords[2];
	Vector3_Add1(&coords[0], &box->Min, -offset);
	Vector3_Add1(&coords[1], &box->Max,  offset);

	Int32 i;
	VertexP3fC4b* ptr;
	PackedCol col = box->Col;

#define SelectionBox_Y(y) 0,y,0, 0,y,1, 1,y,1, 1,y,0,
#define SelectionBox_Z(z) 0,0,z, 0,1,z, 1,1,z, 1,0,z,
#define SelectionBox_X(x) x,0,0, x,1,0, x,1,1, x,0,1,

	static UInt8 faceIndices[72] = {
		SelectionBox_Y(0) SelectionBox_Y(1) /* YMin, YMax */
		SelectionBox_Z(0) SelectionBox_Z(1) /* ZMin, ZMax */
		SelectionBox_X(0) SelectionBox_X(1) /* XMin, XMax */
	};

	ptr = *vertices;
	for (i = 0; i < Array_Elems(faceIndices); i += 3, ptr++) {
		ptr->X   = coords[faceIndices[i + 0]].X;
		ptr->Y   = coords[faceIndices[i + 1]].Y;
		ptr->Z   = coords[faceIndices[i + 2]].Z;
		ptr->Col = col;
	}
	*vertices = ptr;

	col.R = ~col.R; col.G = ~col.G; col.B = ~col.B;
	static UInt8 edgeIndices[72] = {
		0,0,0, 1,0,0,  1,0,0, 1,0,1,  1,0,1, 0,0,1,  0,0,1, 0,0,0, /* YMin */
		0,1,0, 1,1,0,  1,1,0, 1,1,1,  1,1,1, 0,1,1,  0,1,1, 0,1,0, /* YMax */
		0,0,0, 0,1,0,  1,0,0, 1,1,0,  1,0,1, 1,1,1,  0,0,1, 0,1,1, /* X/Z  */
	};

	ptr = *lineVertices;
	for (i = 0; i < Array_Elems(edgeIndices); i += 3, ptr++) {
		ptr->X   = coords[edgeIndices[i + 0]].X;
		ptr->Y   = coords[edgeIndices[i + 1]].Y;
		ptr->Z   = coords[edgeIndices[i + 2]].Z;
		ptr->Col = col;
	}
	*lineVertices = ptr;
}

static Int32 SelectionBox_Compare(struct SelectionBox* a, struct SelectionBox* b) {
	Real32 aDist, bDist;
	if (a->MinDist == b->MinDist) {
		aDist = a->MaxDist; bDist = b->MaxDist;
	} else {
		aDist = a->MinDist; bDist = b->MinDist;
	}

	/* Reversed comparison order result, because we need to render back to front for alpha blending */
	if (aDist < bDist) return 1;
	if (aDist > bDist) return -1;	
	return 0;
}

static void SelectionBox_UpdateDist(Vector3 p, Real32 x2, Real32 y2, Real32 z2, Real32* closest, Real32* furthest) {
	Real32 dx = x2 - p.X, dy = y2 - p.Y, dz = z2 - p.Z;
	Real32 dist = dx * dx + dy * dy + dz * dz;

	if (dist < *closest)  *closest  = dist;
	if (dist > *furthest) *furthest = dist;
}

static void SelectionBox_Intersect(struct SelectionBox* box, Vector3 origin) {
	Vector3 min = box->Min, max = box->Max;
	Real32 closest = MATH_POS_INF, furthest = -MATH_POS_INF;
	/* Bottom corners */
	SelectionBox_UpdateDist(origin, min.X, min.Y, min.Z, &closest, &furthest);
	SelectionBox_UpdateDist(origin, max.X, min.Y, min.Z, &closest, &furthest);
	SelectionBox_UpdateDist(origin, max.X, min.Y, max.Z, &closest, &furthest);
	SelectionBox_UpdateDist(origin, min.X, min.Y, max.Z, &closest, &furthest);
	/* Top corners */
	SelectionBox_UpdateDist(origin, min.X, max.Y, min.Z, &closest, &furthest);
	SelectionBox_UpdateDist(origin, max.X, max.Y, min.Z, &closest, &furthest);
	SelectionBox_UpdateDist(origin, max.X, max.Y, max.Z, &closest, &furthest);
	SelectionBox_UpdateDist(origin, min.X, max.Y, max.Z, &closest, &furthest);
	box->MinDist = closest; box->MaxDist = furthest;
}


#define SELECTIONS_MAX 256
#define SELECTIONS_VERTICES 24
#define SELECTIONS_MAX_VERTICES SELECTIONS_MAX * SELECTIONS_VERTICES

Int32 selections_count;
struct SelectionBox selections_list[SELECTIONS_MAX];
UInt8 selections_ids[SELECTIONS_MAX];
GfxResourceID selections_VB, selections_LineVB;
bool selections_used;

void Selections_Add(UInt8 id, Vector3I p1, Vector3I p2, PackedCol col) {	
	struct SelectionBox sel;
	Vector3I min, max;
	Vector3I_Min(&min, &p1, &p2); Vector3I_ToVector3(&sel.Min, &min);
	Vector3I_Max(&max, &p1, &p2); Vector3I_ToVector3(&sel.Max, &max);
	sel.Col = col;

	Selections_Remove(id);
	selections_list[selections_count] = sel;
	selections_ids[selections_count]  = id;
	selections_count++;
}

void Selections_Remove(UInt8 id) {
	Int32 i;
	for (i = 0; i < selections_count; i++) {
		if (selections_ids[i] != id) continue;

		for (; i < selections_count - 1; i++) {
			selections_list[i] = selections_list[i + 1];
			selections_ids[i]  = selections_ids[i + 1];
		}

		selections_count--;
		return;
	}
}

static void Selections_ContextLost(void* obj) {
	Gfx_DeleteVb(&selections_VB);
	Gfx_DeleteVb(&selections_LineVB);
}

static void Selections_ContextRecreated(void* obj) {
	if (!selections_used) return;
	selections_VB     = Gfx_CreateDynamicVb(VERTEX_FORMAT_P3FC4B, SELECTIONS_MAX_VERTICES);
	selections_LineVB = Gfx_CreateDynamicVb(VERTEX_FORMAT_P3FC4B, SELECTIONS_MAX_VERTICES);
}

static void Selections_QuickSort(Int32 left, Int32 right) {
	UInt8* values = selections_ids; UInt8 value;
	struct SelectionBox* keys = selections_list; struct SelectionBox key;

	while (left < right) {
		Int32 i = left, j = right;
		struct SelectionBox* pivot = &keys[(i + j) / 2];

		/* partition the list */
		while (i <= j) {
			while (SelectionBox_Compare(pivot, &keys[i]) > 0) i++;
			while (SelectionBox_Compare(pivot, &keys[j]) < 0) j--;
			QuickSort_Swap_KV_Maybe();
		}
		/* recurse into the smaller subset */
		QuickSort_Recurse(Selections_QuickSort)
	}
}

void Selections_Render(Real64 delta) {
	if (!selections_count || Gfx_LostContext) return;
	/* TODO: Proper selection box sorting. But this is very difficult because
	   we can have boxes within boxes, intersecting boxes, etc. Probably not worth it. */
	Vector3 camPos = Game_CurrentCameraPos;
	Int32 i;
	for (i = 0; i < selections_count; i++) {
		SelectionBox_Intersect(&selections_list[i], camPos);
	}
	Selections_QuickSort(0, selections_count - 1);

	if (!selections_VB) { /* lazy init as most servers don't use this */
		selections_used = true;
		Selections_ContextRecreated(NULL);
	}

	VertexP3fC4b vertices[SELECTIONS_MAX_VERTICES]; VertexP3fC4b* ptr = vertices;
	VertexP3fC4b lineVertices[SELECTIONS_MAX_VERTICES]; VertexP3fC4b* linePtr = lineVertices;
	for (i = 0; i < selections_count; i++) {	
		SelectionBox_Render(&selections_list[i], &ptr, &linePtr);
	}

	Gfx_SetBatchFormat(VERTEX_FORMAT_P3FC4B);
	GfxCommon_UpdateDynamicVb_Lines(selections_LineVB, lineVertices,
		selections_count * SELECTIONS_VERTICES);

	Gfx_SetDepthWrite(false);
	Gfx_SetAlphaBlending(true);
	GfxCommon_UpdateDynamicVb_IndexedTris(selections_VB, vertices,
		selections_count * SELECTIONS_VERTICES);
	Gfx_SetDepthWrite(true);
	Gfx_SetAlphaBlending(false);
}

static void Selections_Init(void) {
	Event_RegisterVoid(&GfxEvents_ContextLost,      NULL, Selections_ContextLost);
	Event_RegisterVoid(&GfxEvents_ContextRecreated, NULL, Selections_ContextRecreated);
}

static void Selections_Reset(void) {
	selections_count = 0;
}

static void Selections_Free(void) {
	Selections_ContextLost(NULL);
	Event_UnregisterVoid(&GfxEvents_ContextLost,      NULL, Selections_ContextLost);
	Event_UnregisterVoid(&GfxEvents_ContextRecreated, NULL, Selections_ContextRecreated);
}

void Selections_MakeComponent(struct IGameComponent* comp) {
	comp->Init = Selections_Init;
	comp->Free = Selections_Free;
	comp->Reset = Selections_Reset;
	comp->OnNewMap = Selections_Reset;
}