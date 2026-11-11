#pragma once

#include "InputAction.h"

struct UndoData{
	SNSplats* node;
	uint32_t splatcount;
};

struct GizmoUndoAction : public Action{
	// Action either affects a single node 
	shared_ptr<SceneNode> node = nullptr; 

	// or splats in nodes represented by splatmasks
	vector<UndoData> undodatas;

	// forward transformation. Use inverse for undo.
	mat4 transform;

	void undo(){
		if(node){
			node->transform = inverse(transform) * node->transform;
		}else{
			for(auto undoData : undodatas){

				SNSplats* node = undoData.node;

				SplatEditor* editor = SplatEditor::instance;

				editor->applyTransformation(node->dmng.data, inverse(transform), true);
			}
		}
	}

	void redo(){
		if(node){
			node->transform = transform * node->transform;
		}else{
			for(auto undoData : undodatas){

				SNSplats* node = undoData.node;

				SplatEditor* editor = SplatEditor::instance;

				editor->applyTransformation(node->dmng.data, transform, true);
			}
		}
	}

	int64_t byteSize(){
		return 0; // Not enough to matter.
	}

};

struct GizmoAction : public InputAction{

	static const int GIZMO_NONE = 0;
	static const int GIZMO_TRANSLATE = 1;
	static const int GIZMO_ROTATE = 2;
	static const int GIZMO_SCALE = 3;

	shared_ptr<GizmoUndoAction> currentUndoAction = nullptr;
	int mode = GIZMO_NONE;
	mat4 matrix = mat4(1.0f);
	mat4 trans  = mat4(1.0f);
	mat4 delta  = mat4(1.0f);
	mat4 cummulative = mat4(1.0f);
	shared_ptr<SceneNode> node = nullptr;

	int32_t numSelectedSplats = 0;

	bool allowsNavigation(){ return true; }

	void beginUndoable(){
		auto editor = SplatEditor::instance;
		auto node = editor->getSelectedNode();

		currentUndoAction = make_shared<GizmoUndoAction>();

		int32_t numSelectedSplats = editor->getNumSelectedSplats();
		if(numSelectedSplats > 0){
			// create splatmasks for undo action
			editor->scene.process<SNSplats>([&](SNSplats* node){
				uint32_t requiredBits = node->dmng.data.count;
				uint32_t requiredBytes = (requiredBits / 8) + 4;

				UndoData undo;
				undo.node = node;
				undo.splatcount = node->dmng.data.count;
				// undo.cptr_splatmask = cptr_splatmask;

				currentUndoAction->undodatas.push_back(undo);
			});

		}else{
			onTypeMatch<SNSplats>(node, [&](shared_ptr<SNSplats> node){
				currentUndoAction->node = node;
			});
		}
	}

	void endUndoable(){
		auto editor = SplatEditor::instance;

		if(currentUndoAction){
			editor->addAction(currentUndoAction);
			currentUndoAction = nullptr;
		}

	}

	void start(){

		auto editor = SplatEditor::instance;

		node = editor->getSelectedNode();

		numSelectedSplats = editor->getNumSelectedSplats();
		if(numSelectedSplats > 0){
			Box3 aabb = editor->getSelectionAABB();

			vec3 center = (aabb.min + aabb.max) * 0.5f;

			trans = glm::translate(center);
			matrix = trans;
			delta = mat4(1.0f);
		}else if (dynamic_pointer_cast<SNSplats>(node)) {
			shared_ptr<SNSplats> splatNode = dynamic_pointer_cast<SNSplats>(node);
			GaussianData& data = splatNode->dmng.data;

			editor->updateBoundingBox(splatNode.get());

			vec3 center = (node->aabb.min + node->aabb.max) * 0.5f;

			trans = glm::translate(center);
			matrix = trans;
			delta = mat4(1.0f);

		}else if (dynamic_pointer_cast<SNTriangles>(node)) {
			shared_ptr<SNTriangles> triangleNode = dynamic_pointer_cast<SNTriangles>(node);
			TriangleData& data = triangleNode->data;

			// editor->updateBoundingBox(data);
			editor->updateBoundingBox(triangleNode.get());

			vec3 center = (node->aabb.min + node->aabb.max) * 0.5f;
			
			trans = glm::translate(center);
			matrix = trans;
			delta = mat4(1.0f);
		}

	}

	void update(){

		float t_start = 0.0f;
		static CUevent ce_start = 0;
		static CUevent ce_end = 0;
		if(ce_start == 0){
			cuEventCreate(&ce_start, CU_EVENT_DEFAULT);
			cuEventCreate(&ce_end, CU_EVENT_DEFAULT);
		}

		if(Runtime::measureTimings){
			cuEventRecord(ce_start, 0);
			t_start = now();
		}

		auto editor = SplatEditor::instance;
		static ImGuizmo::MODE mCurrentGizmoMode(ImGuizmo::LOCAL);

		ImGuizmo::OPERATION mCurrentGizmoOperation         = ImGuizmo::TRANSLATE;
		if(mode == GIZMO_TRANSLATE) mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
		if(mode == GIZMO_ROTATE)    mCurrentGizmoOperation = ImGuizmo::ROTATE;
		if(mode == GIZMO_SCALE)     mCurrentGizmoOperation = ImGuizmo::SCALE;

		mat4 view = GLRenderer::camera->view;
		mat4 proj = GLRenderer::camera->proj;

		float* fView = (float*)&view;
		float* fProj = (float*)&proj;

		ImGuiIO& io = ImGui::GetIO();

		static bool wasLeftDown = false;
		bool isLeftDown = io.MouseDown[0];
		bool leftReleased = wasLeftDown && !isLeftDown;
		bool leftPressed = wasLeftDown == false && isLeftDown == true;
		wasLeftDown = isLeftDown;

		if(leftPressed && ImGuizmo::IsOver()){
			beginUndoable();
			cummulative = mat4(1.0f);
		}else if(leftReleased){
			endUndoable();
		}
		
		// int32_t numSelectedSplats = editor->getNumSelectedSplats();
		if(numSelectedSplats > 0){

			ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
			if(ImGuizmo::Manipulate(fView, fProj, mCurrentGizmoOperation, mCurrentGizmoMode, (float*)&matrix, (float*)&delta, nullptr)){

				if(mode == GIZMO_SCALE){
					cummulative = (trans * delta * inverse(trans)) * cummulative;
				}else{
					cummulative = delta * cummulative;
				}

				editor->scene.process<SNSplats>([=](SNSplats* node){
					if(node->locked) return;
					if(!node->visible) return;
				
					mat4 t;
					
					if(mode == GIZMO_SCALE){
						// make sure scaling is done from center of gizmo
						t = inverse(node->transform) * trans * delta * inverse(trans) * node->transform;
					}else{
						t = inverse(node->transform_global) * delta * node->transform_global;
					}

					editor->applyTransformation(node->dmng.data, t, true);
				});

				if(currentUndoAction){
					currentUndoAction->transform = cummulative;
				}
			}
		}else if (dynamic_pointer_cast<SNSplats>(node)) {
			shared_ptr<SNSplats> splatNode = dynamic_pointer_cast<SNSplats>(node);
			GaussianData& data = splatNode->dmng.data;

			ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
			if(ImGuizmo::Manipulate(fView, fProj, mCurrentGizmoOperation, mCurrentGizmoMode, (float*)&matrix, (float*)&delta, nullptr)){
				// node->transform = delta * data.transform;
				
				// cummulative = delta * cummulative;

				if(mode == GIZMO_SCALE){
					cummulative = (trans * delta * inverse(trans)) * cummulative;
					node->transform = trans * delta * inverse(trans) * node->transform;
				}else{
					cummulative = delta * cummulative;
					node->transform = delta * node->transform;
				}

				if(currentUndoAction){
					currentUndoAction->transform = cummulative;
				}
			}

		}else if (dynamic_pointer_cast<SNTriangles>(node)) {
			shared_ptr<SNTriangles> triangleNode = dynamic_pointer_cast<SNTriangles>(node);
			// GaussianData& data = splatNode->dmng.data;


			ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
			if(ImGuizmo::Manipulate(fView, fProj, mCurrentGizmoOperation, mCurrentGizmoMode, (float*)&matrix, (float*)&delta, nullptr)){
				// node->transform = delta * data.transform;
				
				// cummulative = delta * cummulative;

				if(mode == GIZMO_SCALE){
					cummulative = (trans * delta * inverse(trans)) * cummulative;
					node->transform = trans * delta * inverse(trans) * node->transform;
				}else{
					cummulative = delta * cummulative;
					node->transform = delta * node->transform;
				}

				if(currentUndoAction){
					currentUndoAction->transform = cummulative;
				}
			}

		}

		if(Runtime::measureTimings){
			cuEventRecord(ce_end, 0);
			float duration_host = now() - t_start;

			cuCtxSynchronize();

			float duration_device = 0.0f;
			cuEventElapsedTime(&duration_device, ce_start, ce_end);

			if(duration_device > 0.005f){
				println("GizmoAction::update() timings: host: {:.3f} ms, device: {:.3f} ms", duration_host, duration_device);
			}
		}

	}

	void stop(){
		endUndoable();
	}

	void makeToolbarSettings(){

		if(Runtime::numSelectedSplats == 0){
			ImGui::Text("Transforming - No splats are selected, so the currently selected layer will be transformed. ");
		}else{
			string msg = format(getSaneLocale(), "Transforming - {:L} splats will be transformed.", Runtime::numSelectedSplats);
			ImGui::Text(msg.c_str());
		}
	}
};