#pragma once

#include "InputAction.h"
#include "GizmoAction.h"

// Volumi di selezione (box / sfera / cilindro)
// - I volumi sono oggetti persistenti in una lista (gruppo "Volumi di selezione" nel pannello a destra, sotto Assets).
// - Si aggiungono cliccando sulla superficie della scena: il volume (lato predefinito 1 m) viene appoggiato
//   sul punto cliccato, con la base orizzontale (asse z in alto).
// - Il volume selezionato si sposta / ruota / scala con il gizmo (t / r / s) o con valori numerici.
// - Le operazioni (seleziona, elimina contenuto, copia / sposta in nuovo layer) agiscono sugli splat
//   il cui centro cade nel volume selezionato, oppure in uno qualsiasi dei volumi visibili.
// - Shape and op constants, VolumeGPU and the intersection helpers are in HostDeviceInterface.h.

struct SelectionVolume{
	string name;
	int shape = VOLUME_SHAPE_BOX;
	mat4 transform = mat4(1.0f);   // unit shape -> world
	bool visible = true;
};

struct VolumeSelectAction : public InputAction{

	static const int GIZMO_TRANSLATE = 0;
	static const int GIZMO_ROTATE    = 1;
	static const int GIZMO_SCALE     = 2;

	// ABGR
	static const uint32_t COLOR_TARGET   = 0xff00b4ff; // orange: volumes the operations act on
	static const uint32_t COLOR_OTHER    = 0xffffc040; // light blue: other volumes
	static const uint32_t COLOR_INVERTED = 0xffff40ff; // magenta: targets in inverted mode
	static const uint32_t COLOR_GHOST    = 0xff60ff60; // green: volume being placed

	inline static vector<shared_ptr<SelectionVolume>> volumes;
	inline static shared_ptr<SelectionVolume> selected = nullptr;
	inline static int counter = 0;

	inline static bool active        = false;  // tool is currently active
	inline static int placingShape   = -1;     // >= 0: next left click places a volume of this shape
	inline static int gizmoMode      = GIZMO_TRANSLATE;
	inline static bool gizmoLocal    = true;
	inline static bool invert        = false;
	inline static bool preview       = true;
	inline static bool applyToAll    = false;
	inline static bool showAlways    = false;
	inline static bool afterCopyMove = true;
	inline static float defaultSize  = 1.0f;   // edge length of newly placed volumes, in meters
	inline static string message     = "";

	// volume being placed (follows the mouse)
	inline static bool ghostValid = false;
	inline static mat4 ghostTransform = mat4(1.0f);

	// gizmo undo
	bool gizmoWasUsing = false;
	mat4 gizmoStartTransform = mat4(1.0f);

	// click picking
	bool leftWasDown = false;
	vec2 leftPressPos = {0.0f, 0.0f};
	bool leftPressOnGizmo = false;

	// face handles: index = 2 * axis + (negative side ? 1 : 0)
	int hoveredHandle = -1;
	int dragHandle = -1;
	mat4 dragStartTransform = mat4(1.0f);
	float dragStartS = 0.0f;

	bool allowsNavigation(){ return true; }
	bool capturesLeftPress(){ return placingShape >= 0 || hoveredHandle >= 0 || dragHandle >= 0; }

	//==================================================================================
	// HELPERS
	//==================================================================================

	static const char* shapeName(int shape){
		if(shape == VOLUME_SHAPE_SPHERE) return "Sfera";
		if(shape == VOLUME_SHAPE_CYLINDER) return "Cilindro";
		return "Box";
	}

	static bool isEditable(SNSplats* node){
		return node->visible && !node->locked;
	}

	static GaussianData getData(SNSplats* node){
		GaussianData data = node->dmng.data;
		data.transform = node->transform_global;
		return data;
	}

	static vector<SNSplats*> getEditableNodes(){
		auto editor = SplatEditor::instance;
		vector<SNSplats*> nodes;

		editor->scene.process<SNSplats>([&](SNSplats* node) {
			if(isEditable(node)) nodes.push_back(node);
		});

		return nodes;
	}

	// Volumes the operations act on
	static vector<shared_ptr<SelectionVolume>> getTargets(){
		vector<shared_ptr<SelectionVolume>> targets;

		if(applyToAll){
			for(auto volume : volumes){
				if(volume->visible) targets.push_back(volume);
			}
		}else if(selected){
			targets.push_back(selected);
		}

		return targets;
	}

	static bool isTarget(shared_ptr<SelectionVolume> volume){
		if(applyToAll) return volume->visible;
		return volume == selected;
	}

	static VolumeGPU toGPU(mat4 transform, int shape, uint32_t color, float alpha){
		VolumeGPU v;
		v.worldToVolume = inverse(transform);
		v.shape = shape;
		v.color = color;
		v.fillAlpha = alpha;
		return v;
	}

	static vector<VolumeGPU> toGPU(vector<shared_ptr<SelectionVolume>> list){
		vector<VolumeGPU> result;
		for(auto volume : list){
			result.push_back(toGPU(volume->transform, volume->shape, 0, 0.0f));
		}
		return result;
	}

	// slot 0: operations, slot 1: rendering
	static CUdeviceptr uploadVolumes(vector<VolumeGPU> list, int slot = 1){
		static CUdeviceptr buffers[2] = {0, 0};
		static int64_t capacities[2] = {0, 0};

		int64_t required = std::max(int64_t(list.size()), int64_t(1)) * sizeof(VolumeGPU);
		if(capacities[slot] < required){
			if(buffers[slot]) CURuntime::free(buffers[slot]);
			int64_t capacity = std::max(required * 2, int64_t(64 * sizeof(VolumeGPU)));
			buffers[slot] = CURuntime::alloc("selection volumes", capacity);
			capacities[slot] = capacity;
		}

		if(list.size() > 0){
			cuMemcpyHtoD(buffers[slot], list.data(), list.size() * sizeof(VolumeGPU));
		}

		return buffers[slot];
	}

	// Volumes to draw as translucent overlay (called from SplatEditor::draw)
	static vector<VolumeGPU> getRenderVolumes(){
		vector<VolumeGPU> list;

		if(!active && !showAlways) return list;

		for(auto volume : volumes){
			if(!volume->visible) continue;

			bool target = isTarget(volume) && active;
			uint32_t color = target ? (invert ? COLOR_INVERTED : COLOR_TARGET) : COLOR_OTHER;
			float alpha = target ? 0.16f : 0.10f;

			list.push_back(toGPU(volume->transform, volume->shape, color, alpha));
		}

		if(active && placingShape >= 0 && ghostValid){
			list.push_back(toGPU(ghostTransform, placingShape, COLOR_GHOST, 0.14f));
		}

		return list;
	}

	static void launchVolumeOp(SNSplats* node, CUdeviceptr cptr_volumes, int numVolumes, int op, CUdeviceptr changedmask){
		auto editor = SplatEditor::instance;

		GaussianData data = getData(node);
		int _invert = invert ? 1 : 0;
		int _op = op;

		void* args[] = {&data, &cptr_volumes, &numVolumes, &_invert, &_op, &changedmask};
		editor->prog_gaussians_editing->launch("kernel_volume_op", args, data.count);
	}

	static void clearHighlighting(){
		auto editor = SplatEditor::instance;

		editor->scene.process<SNSplats>([&](SNSplats* node) {
			auto data = getData(node);
			editor->prog_gaussians_editing->launch("kernel_clear_highlighting", {&editor->launchArgs, &data}, data.count);
		});
	}

	static Box3 getWorldAABB(mat4 transform){
		Box3 box;
		for(int i = 0; i < 8; i++){
			vec3 corner = {
				(i & 1) ? 1.0f : -1.0f,
				(i & 2) ? 1.0f : -1.0f,
				(i & 4) ? 1.0f : -1.0f,
			};
			vec3 p = vec3(transform * vec4(corner, 1.0f));
			box.min = glm::min(box.min, p);
			box.max = glm::max(box.max, p);
		}
		return box;
	}

	// Ray through the mouse position, in world space. Same projection convention as kernel_update_hovered.
	static void getMouseRay(vec3& origin, vec3& dir){
		mat4 view = GLRenderer::camera->view;
		mat4 proj = GLRenderer::camera->proj;
		mat4 invView = inverse(view);

		float px = Runtime::mouseEvents.pos_x;
		float py = Runtime::mouseEvents.pos_y;

		vec3 dir_view = {
			(2.0f * px / float(GLRenderer::width) - 1.0f) / proj[0][0],
			(2.0f * py / float(GLRenderer::height) - 1.0f) / proj[1][1],
			-1.0f
		};

		origin = vec3(invView * vec4(0.0f, 0.0f, 0.0f, 1.0f));
		dir = vec3(invView * vec4(dir_view, 0.0f));
	}

	static shared_ptr<SelectionVolume> pickVolume(){
		vec3 origin, dir;
		getMouseRay(origin, dir);

		shared_ptr<SelectionVolume> closest = nullptr;
		float closestT = 1e30f;

		for(auto volume : volumes){
			if(!volume->visible) continue;

			mat4 worldToVolume = inverse(volume->transform);
			vec3 o = vec3(worldToVolume * vec4(origin, 1.0f));
			vec3 d = vec3(worldToVolume * vec4(dir, 0.0f));

			float t0, t1;
			if(!intersectUnitVolume(volume->shape, o, d, t0, t1)) continue;

			float t = t0 > 0.0f ? t0 : t1;
			if(t < closestT){
				closestT = t;
				closest = volume;
			}
		}

		return closest;
	}

	static mat4 clampScale(mat4 transform){
		float translation[3], rotation[3], scale[3];
		ImGuizmo::DecomposeMatrixToComponents((float*)&transform, translation, rotation, scale);

		bool clamped = false;
		for(int i = 0; i < 3; i++){
			if(scale[i] < 0.0005f){
				scale[i] = 0.0005f;
				clamped = true;
			}
		}

		if(clamped){
			ImGuizmo::RecomposeMatrixFromComponents(translation, rotation, scale, (float*)&transform);
		}

		return transform;
	}

	//==================================================================================
	// VOLUME LIST (undoable)
	//==================================================================================

	static void addVolume_undoable(shared_ptr<SelectionVolume> volume){
		auto editor = SplatEditor::instance;

		volumes.push_back(volume);
		selected = volume;

		editor->addAction({
			.undo = [=](){
				std::erase(volumes, volume);
				if(selected == volume) selected = nullptr;
			},
			.redo = [=](){
				volumes.push_back(volume);
				selected = volume;
			},
		});
	}

	static void removeVolume_undoable(shared_ptr<SelectionVolume> volume){
		auto editor = SplatEditor::instance;

		auto it = std::find(volumes.begin(), volumes.end(), volume);
		if(it == volumes.end()) return;

		int64_t index = it - volumes.begin();
		volumes.erase(it);
		if(selected == volume) selected = nullptr;

		editor->addAction({
			.undo = [=](){
				int64_t i = std::min(index, int64_t(volumes.size()));
				volumes.insert(volumes.begin() + i, volume);
				selected = volume;
			},
			.redo = [=](){
				std::erase(volumes, volume);
				if(selected == volume) selected = nullptr;
			},
		});
	}

	static void addTransform_undoable(shared_ptr<SelectionVolume> volume, mat4 before, mat4 after, int shapeBefore, int shapeAfter){
		auto editor = SplatEditor::instance;

		if(before == after && shapeBefore == shapeAfter) return;

		editor->addAction({
			.undo = [=](){ volume->transform = before; volume->shape = shapeBefore; },
			.redo = [=](){ volume->transform = after;  volume->shape = shapeAfter; },
		});
	}

	static shared_ptr<SelectionVolume> createVolume(int shape, mat4 transform){
		counter++;

		shared_ptr<SelectionVolume> volume = make_shared<SelectionVolume>();
		volume->shape = shape;
		volume->transform = transform;
		volume->name = format("{} {}", shapeName(shape), counter);

		return volume;
	}

	// Volume of size <defaultSize> standing on <pos>, base parallel to the xy plane.
	static mat4 makeStandingTransform(vec3 pos){
		float h = defaultSize * 0.5f;
		return glm::translate(pos + vec3{0.0f, 0.0f, h}) * glm::scale(vec3{h, h, h});
	}

	//==================================================================================
	// OPERATIONS
	//==================================================================================

	static bool checkTargets(){
		if(getTargets().size() == 0){
			message = applyToAll ? "Nessun volume visibile." : "Nessun volume selezionato.";
			return false;
		}
		return true;
	}

	// Selection ops. Undo/redo swaps the stored selection state with the current one.
	static void select_undoable(int op){
		auto editor = SplatEditor::instance;

		if(!checkTargets()) return;

		vector<VolumeGPU> targets = toGPU(getTargets());
		CUdeviceptr cptr_volumes = uploadVolumes(targets, 0);
		int numVolumes = targets.size();

		struct UndoData{
			vector<SNSplats*> nodes;
			vector<CUdeviceptr> masks;

			~UndoData(){
				for(CUdeviceptr cptr : masks){
					CURuntime::free(cptr);
				}
			}
		};

		shared_ptr<UndoData> undodata = make_shared<UndoData>();

		editor->scene.process<SNSplats>([&](SNSplats* node) {
			if(!isEditable(node)) return;
			if(node->dmng.data.count == 0) return;

			int requiredBytes = node->dmng.data.count / 8 + 4;
			CUdeviceptr cptr_mask = CURuntime::alloc("volume selection undo mask", requiredBytes);
			cuMemsetD8(cptr_mask, 0, requiredBytes);

			editor->prog_gaussians_editing->launch("kernel_get_selectionmask", {&node->dmng.data, &cptr_mask}, node->dmng.data.count);

			launchVolumeOp(node, cptr_volumes, numVolumes, op, 0);

			undodata->nodes.push_back(node);
			undodata->masks.push_back(cptr_mask);
		});

		auto swap = [=](){
			auto editor = SplatEditor::instance;
			for(int i = 0; i < undodata->nodes.size(); i++){
				SNSplats* node = undodata->nodes[i];
				void* args[] = {&node->dmng.data, &undodata->masks[i]};
				editor->prog_gaussians_editing->launch("kernel_swap_selection", args, node->dmng.data.count);
			}
		};

		editor->addAction({
			.undo = swap,
			.redo = swap,
		});

		int32_t numSelected = editor->getNumSelectedSplats();
		message = format(getSaneLocale(), "{:L} splat selezionati.", numSelected);
	}

	// Flags splats inside the target volumes as deleted.
	static void delete_undoable(vector<SNSplats*> nodes){
		auto editor = SplatEditor::instance;

		if(!checkTargets()) return;

		vector<VolumeGPU> targets = toGPU(getTargets());
		CUdeviceptr cptr_volumes = uploadVolumes(targets, 0);
		int numVolumes = targets.size();

		struct UndoData{
			vector<SNSplats*> nodes;
			vector<CUdeviceptr> masks;

			~UndoData(){
				for(CUdeviceptr cptr : masks){
					CURuntime::free(cptr);
				}
			}
		};

		shared_ptr<UndoData> undodata = make_shared<UndoData>();

		for(SNSplats* node : nodes){
			if(!isEditable(node)) continue;
			if(node->dmng.data.count == 0) continue;

			int requiredBytes = node->dmng.data.count / 8 + 4;
			CUdeviceptr cptr_mask = CURuntime::alloc("volume deletion mask", requiredBytes);
			cuMemsetD8(cptr_mask, 0, requiredBytes);

			launchVolumeOp(node, cptr_volumes, numVolumes, VOLUME_OP_DELETE, cptr_mask);

			undodata->nodes.push_back(node);
			undodata->masks.push_back(cptr_mask);
		}

		editor->addAction({
			.undo = [=](){
				auto editor = SplatEditor::instance;
				for(int i = 0; i < undodata->nodes.size(); i++){
					SNSplats* node = undodata->nodes[i];
					void* args[] = {&node->dmng.data, &undodata->masks[i]};
					editor->prog_gaussians_editing->launch("kernel_undelete_masked", args, node->dmng.data.count);
				}
			},
			.redo = [=](){
				auto editor = SplatEditor::instance;
				for(int i = 0; i < undodata->nodes.size(); i++){
					SNSplats* node = undodata->nodes[i];
					void* args[] = {&node->dmng.data, &undodata->masks[i]};
					editor->prog_gaussians_editing->launch("kernel_delete_masked", args, node->dmng.data.count);
				}
			},
		});

		editor->getNumSelectedSplats();
	}

	static void deleteInside(){
		if(!checkTargets()) return;
		delete_undoable(getEditableNodes());
		message = "Contenuto eliminato (Ctrl+Z per annullare).";
	}

	// Copies the splats inside the target volumes to a new layer.
	// If <cut> is true, the originals are deleted (= the block is moved to the new layer).
	// Returns the action to switch to afterwards (gizmo on the new layer), or nullptr.
	static shared_ptr<InputAction> copyToNewLayer(bool cut){
		auto editor = SplatEditor::instance;

		if(!checkTargets()) return nullptr;

		vector<SNSplats*> sources = getEditableNodes();

		select_undoable(VOLUME_OP_SELECT_REPLACE);

		int32_t numSelected = editor->getNumSelectedSplats();
		if(numSelected == 0){
			message = "Nessuno splat dentro il volume.";
			return nullptr;
		}

		FilterRules rules;
		rules.selection = FILTER_SELECTION_SELECTED;
		shared_ptr<SNSplats> newNode = editor->filterToNewLayer_undoable(rules);
		newNode->name = cut ? "Blocco spostato" : "Blocco copiato";

		if(cut){
			delete_undoable(sources);
		}

		message = format(getSaneLocale(), "{:L} splat {} nel layer \"{}\".", numSelected, cut ? "spostati" : "copiati", newNode->name);

		if(afterCopyMove){
			// The new layer is now the selected node and none of its splats are selected,
			// so the gizmo transforms the whole new layer.
			shared_ptr<GizmoAction> action = make_shared<GizmoAction>();
			action->mode = GizmoAction::GIZMO_TRANSLATE;
			return action;
		}

		return nullptr;
	}

	static void fitSelectedToSplatSelection(){
		auto editor = SplatEditor::instance;

		if(editor->getNumSelectedSplats() == 0){
			message = "Nessuno splat selezionato.";
			return;
		}

		Box3 box = editor->getSelectionAABB();
		vec3 center = (box.min + box.max) * 0.5f;
		vec3 half = glm::max((box.max - box.min) * 0.5f, vec3{0.001f, 0.001f, 0.001f});
		mat4 transform = glm::translate(center) * glm::scale(half);

		if(selected){
			addTransform_undoable(selected, selected->transform, transform, selected->shape, selected->shape);
			selected->transform = transform;
		}else{
			addVolume_undoable(createVolume(VOLUME_SHAPE_BOX, transform));
		}
	}

	//==================================================================================
	// INPUT ACTION
	//==================================================================================

	// Shortcuts that take precedence over the global ones while this tool is active.
	// Returns true if the key was consumed.
	bool handleShortcuts(){
		bool isShiftDown = Runtime::keyStates[340] != 0;

		if(Runtime::getKeyAction('t') == GLFW_PRESS){ gizmoMode = GIZMO_TRANSLATE; return true; }
		if(Runtime::getKeyAction('r') == GLFW_PRESS){ gizmoMode = GIZMO_ROTATE;    return true; }
		if(Runtime::getKeyAction('s') == GLFW_PRESS){ gizmoMode = GIZMO_SCALE;     return true; }

		if(Runtime::getKeyAction(GLFW_KEY_DELETE) == GLFW_PRESS){
			if(isShiftDown){
				if(selected) removeVolume_undoable(selected);
			}else{
				deleteInside();
			}
			return true;
		}

		if(Runtime::getKeyAction(GLFW_KEY_ESCAPE) == GLFW_PRESS && placingShape >= 0){
			placingShape = -1;
			return true;
		}

		return false;
	}

	void start(){
		active = true;
		ghostValid = false;
	}

	static void drawWireframe(mat4 transform, int shape, uint32_t color){
		auto editor = SplatEditor::instance;

		auto line = [&](vec3 a, vec3 b){
			vec3 wa = vec3(transform * vec4(a, 1.0f));
			vec3 wb = vec3(transform * vec4(b, 1.0f));
			editor->drawLine(wa, wb, color);
		};

		auto circle = [&](auto sample){
			int segments = 48;
			for(int i = 0; i < segments; i++){
				float u0 = 2.0f * 3.14159265f * float(i) / float(segments);
				float u1 = 2.0f * 3.14159265f * float(i + 1) / float(segments);
				line(sample(u0), sample(u1));
			}
		};

		if(shape == VOLUME_SHAPE_BOX){
			for(int i = 0; i < 4; i++){
				float x = (i & 1) ? 1.0f : -1.0f;
				float y = (i & 2) ? 1.0f : -1.0f;
				line({x, y, -1.0f}, {x, y, 1.0f});
				line({-1.0f, x, y}, {1.0f, x, y});
				line({x, -1.0f, y}, {x, 1.0f, y});
			}
		}else if(shape == VOLUME_SHAPE_SPHERE){
			circle([](float u){ return vec3{cos(u), sin(u), 0.0f}; });
			circle([](float u){ return vec3{cos(u), 0.0f, sin(u)}; });
			circle([](float u){ return vec3{0.0f, cos(u), sin(u)}; });
		}else if(shape == VOLUME_SHAPE_CYLINDER){
			circle([](float u){ return vec3{cos(u), sin(u), -1.0f}; });
			circle([](float u){ return vec3{cos(u), sin(u),  1.0f}; });
			line({ 1.0f,  0.0f, -1.0f}, { 1.0f,  0.0f, 1.0f});
			line({-1.0f,  0.0f, -1.0f}, {-1.0f,  0.0f, 1.0f});
			line({ 0.0f,  1.0f, -1.0f}, { 0.0f,  1.0f, 1.0f});
			line({ 0.0f, -1.0f, -1.0f}, { 0.0f, -1.0f, 1.0f});
		}
	}

	void updatePlacement(){
		auto editor = SplatEditor::instance;

		ghostValid = false;

		float depth = editor->deviceState.hovered_depth;
		bool hasSurface = std::isfinite(depth) && depth > 0.0f && depth < 1e30f;

		vec3 pos;
		if(hasSurface){
			pos = editor->deviceState.hovered_pos;
			ghostValid = true;
		}else{
			// no surface under the mouse: use the ground plane z = 0
			vec3 origin, dir;
			getMouseRay(origin, dir);
			if(std::abs(dir.z) > 1e-6f){
				float t = -origin.z / dir.z;
				if(t > 0.0f){
					pos = origin + t * dir;
					ghostValid = true;
				}
			}
		}

		if(!ghostValid) return;

		ghostTransform = makeStandingTransform(pos);

		if(Runtime::mouseEvents.isLeftDownEvent()){
			bool isShiftDown = Runtime::keyStates[340] != 0;

			shared_ptr<SelectionVolume> volume = createVolume(placingShape, ghostTransform);
			addVolume_undoable(volume);
			message = format("Aggiunto \"{}\".", volume->name);

			// Shift: keep placing
			if(!isShiftDown){
				placingShape = -1;
				ghostValid = false;
			}
		}
	}

	void updateGizmo(){
		if(!selected || !selected->visible){
			gizmoWasUsing = false;
			return;
		}

		ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
		if(gizmoMode == GIZMO_ROTATE) operation = ImGuizmo::ROTATE;
		if(gizmoMode == GIZMO_SCALE)  operation = ImGuizmo::SCALE;

		ImGuizmo::MODE mode = gizmoLocal ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
		// ImGuizmo only scales in local space
		if(gizmoMode == GIZMO_SCALE) mode = ImGuizmo::LOCAL;

		mat4 view = GLRenderer::camera->view;
		mat4 proj = GLRenderer::camera->proj;

		if(!gizmoWasUsing){
			gizmoStartTransform = selected->transform;
		}

		ImGuiIO& io = ImGui::GetIO();
		ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
		ImGuizmo::Manipulate((float*)&view, (float*)&proj, operation, mode, (float*)&selected->transform);

		bool isUsing = ImGuizmo::IsUsing();

		if(isUsing){
			selected->transform = clampScale(selected->transform);
		}

		if(gizmoWasUsing && !isUsing){
			addTransform_undoable(selected, gizmoStartTransform, selected->transform, selected->shape, selected->shape);
		}

		gizmoWasUsing = isUsing;
	}

	// world -> ImGui screen coordinates (y down)
	static bool projectToScreen(vec3 p, ImVec2& out){
		mat4 view = GLRenderer::camera->view;
		mat4 proj = GLRenderer::camera->proj;
		vec4 clip = proj * view * vec4(p, 1.0f);
		if(clip.w <= 0.0f) return false;

		ImGuiIO& io = ImGui::GetIO();
		float nx = clip.x / clip.w;
		float ny = clip.y / clip.w;
		out.x = (nx * 0.5f + 0.5f) * io.DisplaySize.x;
		out.y = (1.0f - (ny * 0.5f + 0.5f)) * io.DisplaySize.y;
		return true;
	}

	// Parameter s of the point on the line P0 + s * a that is closest to the mouse ray. False if (nearly) parallel.
	static bool closestOnAxis(vec3 P0, vec3 a, float& s){
		vec3 o, d;
		getMouseRay(o, d);
		d = normalize(d);

		vec3 w0 = P0 - o;
		float B = dot(a, d);
		float D = dot(a, w0);
		float E = dot(d, w0);
		float denom = 1.0f - B * B;
		if(denom < 1e-4f) return false;

		s = (B * E - D) / denom;
		return true;
	}

	// One handle per face: dragging it moves that face only, the opposite face stays in place.
	void updateFaceHandles(){
		if(!selected || !selected->visible){
			hoveredHandle = -1;
			dragHandle = -1;
			return;
		}

		ImGuiIO& io = ImGui::GetIO();
		ImVec2 mouse = io.MousePos;
		mat4 transform = selected->transform;

		vec3 positions[6];
		vec3 axes[6];
		ImVec2 screen[6];
		bool onScreen[6];

		for(int h = 0; h < 6; h++){
			int axis = h / 2;
			float sign = (h % 2 == 0) ? 1.0f : -1.0f;
			vec3 local = {0.0f, 0.0f, 0.0f};
			local[axis] = sign;

			positions[h] = vec3(transform * vec4(local, 1.0f));
			axes[h] = normalize(vec3(transform * vec4(local, 0.0f)));
			onScreen[h] = projectToScreen(positions[h], screen[h]);
		}

		// hover
		if(dragHandle < 0){
			hoveredHandle = -1;
			bool heldDown = Runtime::mouseEvents.isLeftDown && !Runtime::mouseEvents.isLeftDownEvent();
			bool blocked = io.WantCaptureMouse || ImGuizmo::IsUsing() || heldDown;
			if(!blocked){
				float closest = 10.0f;
				for(int h = 0; h < 6; h++){
					if(!onScreen[h]) continue;
					float dx = screen[h].x - mouse.x;
					float dy = screen[h].y - mouse.y;
					float dist = sqrt(dx * dx + dy * dy);
					if(dist < closest){
						closest = dist;
						hoveredHandle = h;
					}
				}
			}
		}

		// start drag
		if(dragHandle < 0 && hoveredHandle >= 0 && Runtime::mouseEvents.isLeftDownEvent()){
			float s;
			if(closestOnAxis(positions[hoveredHandle], axes[hoveredHandle], s)){
				dragHandle = hoveredHandle;
				dragStartTransform = transform;
				dragStartS = s;
			}
		}

		// drag
		if(dragHandle >= 0){
			int axis = dragHandle / 2;
			float sign = (dragHandle % 2 == 0) ? 1.0f : -1.0f;
			vec3 local = {0.0f, 0.0f, 0.0f};
			local[axis] = sign;

			vec3 P0 = vec3(dragStartTransform * vec4(local, 1.0f));
			vec3 a = vec3(dragStartTransform * vec4(local, 0.0f));
			float halfExtent = length(a);
			a = a / halfExtent;

			float s;
			if(closestOnAxis(P0, a, s)){
				float delta = s - dragStartS;
				float newHalf = std::max(halfExtent + delta * 0.5f, 0.0005f);
				float applied = (newHalf - halfExtent) * 2.0f;

				vec3 factor = {1.0f, 1.0f, 1.0f};
				factor[axis] = newHalf / halfExtent;

				selected->transform = glm::translate(a * (applied * 0.5f)) * dragStartTransform * glm::scale(factor);
			}

			if(!Runtime::mouseEvents.isLeftDown){
				addTransform_undoable(selected, dragStartTransform, selected->transform, selected->shape, selected->shape);
				dragHandle = -1;
			}
		}

		// draw
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		ImU32 axisColors[3] = {IM_COL32(230, 60, 60, 255), IM_COL32(60, 200, 60, 255), IM_COL32(70, 110, 240, 255)};

		for(int h = 0; h < 6; h++){
			if(!onScreen[h]) continue;

			bool highlighted = (h == hoveredHandle && dragHandle < 0) || h == dragHandle;
			ImU32 color = highlighted ? IM_COL32(255, 220, 0, 255) : axisColors[h / 2];
			float radius = highlighted ? 8.0f : 6.0f;

			drawList->AddCircleFilled(screen[h], radius + 1.5f, IM_COL32(0, 0, 0, 200));
			drawList->AddCircleFilled(screen[h], radius, color);
		}
	}

	void updatePicking(){
		bool isLeftDown = Runtime::mouseEvents.isLeftDown;
		vec2 mousePos = {Runtime::mouseEvents.pos_x, Runtime::mouseEvents.pos_y};

		if(!leftWasDown && isLeftDown){
			leftPressPos = mousePos;
			leftPressOnGizmo = ImGuizmo::IsOver() || ImGuizmo::IsUsing() || hoveredHandle >= 0 || dragHandle >= 0;
		}else if(leftWasDown && !isLeftDown){
			bool isClick = length(mousePos - leftPressPos) < 4.0f;
			if(isClick && !leftPressOnGizmo){
				shared_ptr<SelectionVolume> picked = pickVolume();
				if(picked) selected = picked;
			}
		}

		leftWasDown = isLeftDown;
	}

	void update(){
		auto editor = SplatEditor::instance;

		active = true;

		if(placingShape >= 0){
			updatePlacement();
			leftWasDown = Runtime::mouseEvents.isLeftDown;
		}else{
			ghostValid = false;
			updatePicking();

			// The scale mode uses only the face handles (non-uniform scaling, opposite face fixed),
			// translate and rotate use only the gizmo.
			if(gizmoMode == GIZMO_SCALE){
				updateFaceHandles();
				gizmoWasUsing = false;
			}else{
				hoveredHandle = -1;
				dragHandle = -1;
				updateGizmo();
			}
		}

		// wireframes
		for(auto volume : volumes){
			if(!volume->visible) continue;

			bool target = isTarget(volume);
			uint32_t color = target ? (invert ? COLOR_INVERTED : COLOR_TARGET) : COLOR_OTHER;
			drawWireframe(volume->transform, volume->shape, color);
		}

		// preview: highlight splats inside the target volumes
		static bool wasPreview = false;
		vector<shared_ptr<SelectionVolume>> targets = getTargets();
		if(preview && targets.size() > 0){
			vector<VolumeGPU> gpuTargets = toGPU(targets);
			CUdeviceptr cptr_volumes = uploadVolumes(gpuTargets, 0);
			int numVolumes = gpuTargets.size();

			editor->scene.process<SNSplats>([&](SNSplats* node) {
				if(!isEditable(node)) return;
				launchVolumeOp(node, cptr_volumes, numVolumes, VOLUME_OP_PREVIEW, 0);
			});
			wasPreview = true;
		}else if(wasPreview){
			clearHighlighting();
			wasPreview = false;
		}
	}

	void stop(){
		active = false;
		placingShape = -1;
		ghostValid = false;
		clearHighlighting();
	}

	//==================================================================================
	// GUI
	//==================================================================================

	// Buttons shared by the toolbar and the panel. Switching to another action is deferred via <next>.
	static void makeOperationButtons(shared_ptr<InputAction>& next){
		if(ImGui::Button("Seleziona")){
			select_undoable(VOLUME_OP_SELECT_REPLACE);
		}
		if(ImGui::IsItemHovered()) ImGui::SetTooltip("Sostituisce la selezione con gli splat nel volume.");

		ImGui::SameLine();
		if(ImGui::Button("+ Sel.")){
			select_undoable(VOLUME_OP_SELECT_ADD);
		}
		if(ImGui::IsItemHovered()) ImGui::SetTooltip("Aggiunge alla selezione gli splat nel volume.");

		ImGui::SameLine();
		if(ImGui::Button("- Sel.")){
			select_undoable(VOLUME_OP_SELECT_REMOVE);
		}
		if(ImGui::IsItemHovered()) ImGui::SetTooltip("Toglie dalla selezione gli splat nel volume.");

		ImGui::SameLine();
		if(ImGui::Button("Elimina contenuto")){
			deleteInside();
		}
		if(ImGui::IsItemHovered()) ImGui::SetTooltip("Elimina gli splat nel volume (o fuori, con Inverti). Tasto Canc. Annullabile con Ctrl+Z.");

		if(ImGui::Button("Copia in nuovo layer")){
			next = copyToNewLayer(false);
		}
		if(ImGui::IsItemHovered()) ImGui::SetTooltip("Copia gli splat del volume in un nuovo layer, poi lo puoi spostare / ruotare col gizmo (t / r / s).");

		ImGui::SameLine();
		if(ImGui::Button("Sposta in nuovo layer")){
			next = copyToNewLayer(true);
		}
		if(ImGui::IsItemHovered()) ImGui::SetTooltip("Come Copia, ma elimina gli originali: il blocco viene staccato dalla scena.");
	}

	void makeToolbarSettings(){

		ImGui::Text("Aggiungi (click sulla scena):");
		ImGui::SameLine();
		ImGui::RadioButton("Box", &placingShape, VOLUME_SHAPE_BOX);
		ImGui::SameLine();
		ImGui::RadioButton("Sfera", &placingShape, VOLUME_SHAPE_SPHERE);
		ImGui::SameLine();
		ImGui::RadioButton("Cilindro", &placingShape, VOLUME_SHAPE_CYLINDER);
		ImGui::SameLine();
		ImGui::RadioButton("Nessuno", &placingShape, -1);

		ImGui::SameLine();
		ImGui::Text("   Gizmo:");
		ImGui::SameLine();
		ImGui::RadioButton("Sposta (t)", &gizmoMode, GIZMO_TRANSLATE);
		ImGui::SameLine();
		ImGui::RadioButton("Ruota (r)", &gizmoMode, GIZMO_ROTATE);
		ImGui::SameLine();
		ImGui::RadioButton("Scala (s)", &gizmoMode, GIZMO_SCALE);
		if(ImGui::IsItemHovered()) ImGui::SetTooltip("Maniglie sulle facce: trascina per ridimensionare ogni lato separatamente.");
		ImGui::SameLine();
		ImGui::Checkbox("Assi locali", &gizmoLocal);

		ImGui::SameLine();
		ImGui::Text("   ");
		ImGui::SameLine();
		ImGui::Checkbox("Anteprima", &preview);
		ImGui::SameLine();
		ImGui::Checkbox("Inverti (agisci fuori)", &invert);

		makeOperationButtons(nextAction);

		ImGui::SameLine();
		if(message.size() > 0){
			ImGui::Text("   %s", message.c_str());
		}

		if(placingShape >= 0){
			ImGui::TextDisabled("Click sinistro sulla superficie: piazza il volume (Shift + click: piazzane altri).  Esc: annulla.");
		}else{
			ImGui::TextDisabled("Scala (s): trascina le maniglie sulle facce.  Click su un volume: selezionalo.  Canc: elimina contenuto.  Shift+Canc: elimina volume.  Destro trascinato: sposta vista.  Click destro / Esc / Vol: esci.");
		}
	}

	// Separate group in the "Editing" window on the right, below the assets.
	static void makePanel(){
		auto editor = SplatEditor::instance;

		if(!ImGui::CollapsingHeader("Volumi di selezione", ImGuiTreeNodeFlags_DefaultOpen)) return;

		shared_ptr<InputAction> next = nullptr;

		auto startPlacing = [&](int shape){
			if(!active){
				editor->setAction(make_shared<VolumeSelectAction>());
			}
			placingShape = shape;
		};

		if(ImGui::Button("+ Box")) startPlacing(VOLUME_SHAPE_BOX);
		ImGui::SameLine();
		if(ImGui::Button("+ Sfera")) startPlacing(VOLUME_SHAPE_SPHERE);
		ImGui::SameLine();
		if(ImGui::Button("+ Cilindro")) startPlacing(VOLUME_SHAPE_CYLINDER);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(70.0f);
		ImGui::DragFloat("lato (m)", &defaultSize, 0.01f, 0.01f, 1000.0f, "%.2f");
		if(ImGui::IsItemHovered()) ImGui::SetTooltip("Dimensione dei nuovi volumi. Dopo \"+ Box\" cliccare sulla superficie della scena.");

		if(active){
			if(ImGui::Button("Disattiva strumento")){
				next = nullptr;
				editor->setAction(nullptr);
			}
		}else{
			if(ImGui::Button("Attiva strumento (v)")){
				editor->setAction(make_shared<VolumeSelectAction>());
			}
		}
		ImGui::SameLine();
		ImGui::Checkbox("Mostra sempre", &showAlways);
		if(ImGui::IsItemHovered()) ImGui::SetTooltip("Mostra i volumi anche quando lo strumento non e' attivo.");

		// LIST
		shared_ptr<SelectionVolume> toRemove = nullptr;

		ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY;
		float rowHeight = ImGui::GetFrameHeightWithSpacing();
		float tableHeight = std::clamp(float(volumes.size()) * rowHeight + 4.0f, rowHeight + 4.0f, 8.0f * rowHeight);

		if(volumes.size() == 0){
			ImGui::TextDisabled("Nessun volume. Premi \"+ Box\" e clicca sulla scena.");
		}else if(ImGui::BeginTable("volumes_table", 3, tableFlags, ImVec2(0.0f, tableHeight))){
			ImGui::TableSetupColumn("vis", ImGuiTableColumnFlags_WidthFixed, 24.0f);
			ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("del", ImGuiTableColumnFlags_WidthFixed, 24.0f);

			for(auto volume : volumes){
				ImGui::PushID(volume.get());
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				ImGui::Checkbox("##visible", &volume->visible);
				if(ImGui::IsItemHovered()) ImGui::SetTooltip("Visibile (i volumi nascosti non partecipano alle operazioni su \"tutti\")");

				ImGui::TableNextColumn();
				string label = format("{}  [{}]", volume->name, shapeName(volume->shape));
				if(ImGui::Selectable(label.c_str(), volume == selected)){
					selected = volume;
					if(!active) editor->setAction(make_shared<VolumeSelectAction>());
				}
				if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)){
					Box3 box = getWorldAABB(volume->transform);
					Runtime::controls->focus(box.min, box.max, 1.0f);
				}

				ImGui::TableNextColumn();
				if(ImGui::SmallButton("X")){
					toRemove = volume;
				}
				if(ImGui::IsItemHovered()) ImGui::SetTooltip("Elimina il volume (non il contenuto)");

				ImGui::PopID();
			}

			ImGui::EndTable();
		}

		if(toRemove){
			removeVolume_undoable(toRemove);
		}

		// SELECTED VOLUME PROPERTIES
		if(selected){
			ImGui::Separator();

			static mat4 editStartTransform = mat4(1.0f);
			static int editStartShape = 0;
			if(!ImGui::IsAnyItemActive()){
				editStartTransform = selected->transform;
				editStartShape = selected->shape;
			}

			char nameBuffer[256];
			snprintf(nameBuffer, sizeof(nameBuffer), "%s", selected->name.c_str());
			ImGui::SetNextItemWidth(200.0f);
			if(ImGui::InputText("Nome", nameBuffer, sizeof(nameBuffer))){
				selected->name = nameBuffer;
			}

			ImGui::SameLine();
			ImGui::SetNextItemWidth(100.0f);
			const char* shapes[] = {"Box", "Sfera", "Cilindro"};
			int shape = selected->shape;
			if(ImGui::Combo("Forma", &shape, shapes, 3)){
				addTransform_undoable(selected, selected->transform, selected->transform, selected->shape, shape);
				selected->shape = shape;
			}

			float translation[3], rotation[3], scale[3];
			ImGuizmo::DecomposeMatrixToComponents((float*)&selected->transform, translation, rotation, scale);
			float size[3] = {2.0f * scale[0], 2.0f * scale[1], 2.0f * scale[2]};

			bool changed = false;
			bool editFinished = false;

			changed |= ImGui::DragFloat3("Posizione (m)", translation, 0.01f, 0.0f, 0.0f, "%.3f");
			editFinished |= ImGui::IsItemDeactivatedAfterEdit();
			changed |= ImGui::DragFloat3("Rotazione (gradi)", rotation, 0.5f, 0.0f, 0.0f, "%.1f");
			editFinished |= ImGui::IsItemDeactivatedAfterEdit();
			changed |= ImGui::DragFloat3("Dimensioni (m)", size, 0.01f, 0.001f, 100000.0f, "%.3f");
			editFinished |= ImGui::IsItemDeactivatedAfterEdit();

			if(changed){
				for(int i = 0; i < 3; i++){
					scale[i] = std::max(size[i], 0.001f) / 2.0f;
				}
				ImGuizmo::RecomposeMatrixFromComponents(translation, rotation, scale, (float*)&selected->transform);
			}

			if(editFinished){
				addTransform_undoable(selected, editStartTransform, selected->transform, editStartShape, selected->shape);
			}

			if(ImGui::Button("Duplica")){
				shared_ptr<SelectionVolume> copy = createVolume(selected->shape, selected->transform);
				addVolume_undoable(copy);
			}
			ImGui::SameLine();
			if(ImGui::Button("Raddrizza")){
				mat4 before = selected->transform;
				ImGuizmo::DecomposeMatrixToComponents((float*)&selected->transform, translation, rotation, scale);
				rotation[0] = rotation[1] = rotation[2] = 0.0f;
				ImGuizmo::RecomposeMatrixFromComponents(translation, rotation, scale, (float*)&selected->transform);
				addTransform_undoable(selected, before, selected->transform, selected->shape, selected->shape);
			}
			if(ImGui::IsItemHovered()) ImGui::SetTooltip("Azzera la rotazione.");
			ImGui::SameLine();
			if(ImGui::Button("Adatta alla selezione")){
				fitSelectedToSplatSelection();
			}
			if(ImGui::IsItemHovered()) ImGui::SetTooltip("Adatta il volume al box degli splat selezionati.");
			ImGui::SameLine();
			if(ImGui::Button("Inquadra")){
				Box3 box = getWorldAABB(selected->transform);
				Runtime::controls->focus(box.min, box.max, 1.0f);
			}
		}

		// OPERATIONS
		ImGui::Separator();

		int applyMode = applyToAll ? 1 : 0;
		ImGui::Text("Agisci su:");
		ImGui::SameLine();
		ImGui::RadioButton("volume selezionato", &applyMode, 0);
		ImGui::SameLine();
		ImGui::RadioButton("tutti i visibili", &applyMode, 1);
		applyToAll = applyMode == 1;

		ImGui::Checkbox("Anteprima##panel", &preview);
		ImGui::SameLine();
		ImGui::Checkbox("Inverti (agisci fuori dai volumi)", &invert);

		ImGui::PushID("panel_ops");
		makeOperationButtons(next);
		ImGui::PopID();
		ImGui::SameLine();
		ImGui::Checkbox("poi gizmo", &afterCopyMove);
		if(ImGui::IsItemHovered()) ImGui::SetTooltip("Dopo Copia / Sposta attiva il gizmo sul nuovo layer.");

		if(message.size() > 0){
			ImGui::TextWrapped("%s", message.c_str());
		}

		if(ImGui::IsAnyItemActive()){
			editor->temporarilyDisableShortcuts(2);
		}

		if(next){
			editor->setAction(next);
		}
	}

};
