
#include "actions/BrushSelectAction.h"
#include "actions/SphereSelectAction.h"
#include "actions/SpherePaintAction.h"
#include "actions/RectSelectAction.h"
#include "actions/ThreePointAlignAction.h"
#include "actions/GizmoAction.h"
#include "actions/VolumeSelectAction.h"

void SplatEditor::setAction(shared_ptr<InputAction> action){
	auto editor = SplatEditor::instance;
	auto& state = editor->state;

	if(state.currentAction){
		state.currentAction->stop();
	}

	state.currentAction = action;

	if(action){
		action->start();
	}
}

void SplatEditor::inputHandlingDesktop(){

	auto editor = SplatEditor::instance;
	auto& settings = editor->settings;
	auto& state = editor->state;
	auto& scene = editor->scene;
	auto& launchArgs = editor->launchArgs;

	static bool wasSpaceDown = false;
	bool isSpaceDown       = Runtime::keyStates[32] != 0;

	// println("Runtime::keyStates[32]: {}, isDown: {}, wasDown: {}", Runtime::keyStates[32], isSpaceDown, wasSpaceDown);

	bool isCtrlDown        = Runtime::keyStates[341] != 0;
	bool isAltDown         = Runtime::keyStates[342] != 0;
	bool isShiftDown       = Runtime::keyStates[340] != 0;
	bool isLeftClicked     = Runtime::mouseEvents.button == 0 && Runtime::mouseEvents.action == 1;
	static bool isLeftDown = false;
	bool isRightClicked    = false; // right click event: press and release without move

	// CHECK SHORTCUTS
	if(!areShortcutsDisabled() && Runtime::frame_keys.size() > 0){

		// int key_y = glfwGetKeyScancode(GLFW_KEY_Y);
		// int key_z = glfwGetKeyScancode(GLFW_KEY_Z);
		// println("y: {}, z: {}", key_y, key_z);

		auto volumeAction = dynamic_pointer_cast<VolumeSelectAction>(state.currentAction);

		if(volumeAction && volumeAction->handleShortcuts()){
			// consumed by the volume selection tool
		}else if(Runtime::getKeyAction('t') == GLFW_PRESS){
			shared_ptr<GizmoAction> action = make_shared<GizmoAction>();
			action->mode = GizmoAction::GIZMO_TRANSLATE;
			setAction(action);
		}else if(Runtime::getKeyAction('r') == GLFW_PRESS){
			shared_ptr<GizmoAction> action = make_shared<GizmoAction>();
			action->mode = GizmoAction::GIZMO_ROTATE;
			setAction(action);
		}else if(Runtime::getKeyAction('s') == GLFW_PRESS){
			shared_ptr<GizmoAction> action = make_shared<GizmoAction>();
			action->mode = GizmoAction::GIZMO_SCALE;
			setAction(action);
		}else if(Runtime::getKeyAction('c') == GLFW_PRESS){
			// shared_ptr<MagicWandAction> action = make_shared<MagicWandAction>();
			// setAction(action);
		}else if(Runtime::getKeyAction('b') == GLFW_PRESS){
			shared_ptr<SpherePaintAction> action = make_shared<SpherePaintAction>();
			setAction(action);
		}else if(Runtime::getKeyAction('1') == GLFW_PRESS){
			shared_ptr<BrushSelectAction> action = make_shared<BrushSelectAction>();
			setAction(action);
			settings.brush.mode = BRUSHMODE::SELECT;
		}else if(Runtime::getKeyAction('2') == GLFW_PRESS){
			shared_ptr<BrushSelectAction> action = make_shared<BrushSelectAction>();
			setAction(action);
			settings.brush.mode = BRUSHMODE::ERASE;
		}else if(Runtime::getKeyAction('v') == GLFW_PRESS){
			// toggle the volume selection tool
			if(volumeAction){
				setAction(nullptr);
			}else{
				setAction(make_shared<VolumeSelectAction>());
			}
		}else if(Runtime::getKeyAction(256) == GLFW_PRESS){
			// "esc" key: end the current tool
			setAction(nullptr);
		}else if(Runtime::getKeyAction('3') == GLFW_PRESS){
			
		}else if(Runtime::getKeyAction(261) == GLFW_PRESS){
			// "del" key
			editor->deleteSelection_undoable();
		}else if(isCtrlDown && Runtime::getKeyAction('d') == GLFW_PRESS){

			auto numSelected = editor->getNumSelectedSplats();
			if(numSelected > 0){
				// Duplicate selected splats
				FilterRules rules;
				rules.selection = FILTER_SELECTION_SELECTED;
				editor->filterToNewLayer_undoable(rules);
			}else{
				// Duplicate selected layer
				auto node = editor->getSelectedNode();

				onTypeMatch<SNSplats>(node, [=](shared_ptr<SNSplats> node){
					editor->duplicateLayer_undoable(node);
				});
			}
		}else if(isCtrlDown && Runtime::getKeyAction('a') == GLFW_PRESS){
			editor->selectAll_undoable();
		}else if(isCtrlDown && Runtime::getKeyAction('e') == GLFW_PRESS){

			shared_ptr<SceneNode> selected = editor->getSelectedNode();

			vector<shared_ptr<SceneNode>> layers = editor->getLayers();
			for(int i = 0; i < layers.size(); i++){
				bool isLast = i == layers.size() - 1;
				if(layers[i] == selected && !isLast){
					shared_ptr<SceneNode> next = layers[i + 1];

					editor->merge_undoable(selected, next);

					break;
				}
			}
		}else if (isCtrlDown && Runtime::getKeyAction('z') == GLFW_PRESS) {
			editor->undo();
		}else if (isCtrlDown && Runtime::getKeyAction('y') == GLFW_PRESS) {
			editor->redo();
		}if(isSpaceDown && !wasSpaceDown){
			
			if(settings.splatRenderer == SPLATRENDERER_3DGS){
				settings.splatRenderer = SPLATRENDERER_PERSPECTIVE_CORRECT;
			}else{
				settings.splatRenderer = SPLATRENDERER_3DGS;
			}

		}

		wasSpaceDown = isSpaceDown;
	}

	// zoom to double-clicked location
	if(ImGui::IsMouseDoubleClicked(0)){
		
		vec3 pos = editor->deviceState.hovered_pos;
		float depth = editor->deviceState.hovered_depth;

		if(depth > 0.0f && depth != Infinity){

			vec3 start_target  = Runtime::controls->target;
			float start_radius = Runtime::controls->radius;
			vec3 end_target    = pos;
			float end_radius   = Runtime::controls->radius * 0.3f;

			TWEEN::animate(0.1, [=](float u){
				vec3 target = (1.0f - u) * start_target + u * end_target;
				float radius = (1.0f - u) * start_radius + u * end_radius;

				Runtime::controls->target = target;
				Runtime::controls->radius = radius;
			});
		}
	}

	static struct {
		vec2 startPos;
		bool isRightDown = false;
		bool hasMoved = false;
	} rightDownState;

	if(!rightDownState.isRightDown && Runtime::mouseEvents.isRightDown){
		// right mouse just pressed
		rightDownState.startPos = {Runtime::mouseEvents.pos_x, Runtime::mouseEvents.pos_y};
		rightDownState.hasMoved = false;
		rightDownState.isRightDown = true;
	}else if(rightDownState.isRightDown && Runtime::mouseEvents.isRightDown){
		// right mouse still pressed
		if(rightDownState.startPos.x != Runtime::mouseEvents.pos_x || rightDownState.startPos.y != Runtime::mouseEvents.pos_y){
			rightDownState.hasMoved = true;
		}
	}else if(rightDownState.isRightDown && !Runtime::mouseEvents.isRightDown){
		// right mouse just released
		rightDownState.isRightDown = false;

		isRightClicked = rightDownState.hasMoved == false;
	}

	if(Runtime::mouseEvents.isLeftDownEvent()) isLeftDown = true;
	if(Runtime::mouseEvents.isLeftUpEvent()) isLeftDown = false;

	if(state.currentAction && state.currentAction->allowsNavigation()){
		// Tools with gizmos: the camera can still be navigated. 
		// A right click without moving ends the tool, a right drag pans.
		static bool leftPressStartedOnGizmo = false;
		if(Runtime::mouseEvents.isLeftDownEvent()) leftPressStartedOnGizmo = ImGuizmo::IsOver() || ImGuizmo::IsUsing() || state.currentAction->capturesLeftPress();
		if(!Runtime::mouseEvents.isLeftDown) leftPressStartedOnGizmo = false;

		if(isRightClicked){
			setAction(nullptr);
		}else{
			state.currentAction->update();

			bool gizmoBusy = ImGuizmo::IsUsing() || leftPressStartedOnGizmo;
			if(gizmoBusy){
				Runtime::controls->mousePos = {Runtime::mouseEvents.pos_x, Runtime::mouseEvents.pos_y};
			}else{
				Runtime::controls->onMouseMove(Runtime::mouseEvents.pos_x, Runtime::mouseEvents.pos_y);
				Runtime::controls->onMouseScroll(Runtime::mouseEvents.wheel_x, Runtime::mouseEvents.wheel_y);
				Runtime::controls->update();
			}
		}
	}else if(state.currentAction){
		if(Runtime::mouseEvents.isRightDown){
			setAction(nullptr);

			// hack: "hasMoved" basically consumes the right click, 
			// and we want it to be consumed in this case so that we don't spawn a context menu.
			rightDownState.hasMoved = true;
		}else{
			state.currentAction->update();
		}

		Runtime::controls->mousePos = {Runtime::mouseEvents.pos_x, Runtime::mouseEvents.pos_y};
	}else if(state.currentAction == nullptr && !isRightClicked){
		// UPDATE NAVIGATION if no action 
		Runtime::controls->onMouseMove(Runtime::mouseEvents.pos_x, Runtime::mouseEvents.pos_y);
		Runtime::controls->onMouseScroll(Runtime::mouseEvents.wheel_x, Runtime::mouseEvents.wheel_y);
		Runtime::controls->update();
	}else if(state.currentAction == nullptr && isRightClicked){
		// OPEN CONTEXT MENU
		println("open context menu");
		settings.openContextMenu = true;
	}

	// An action may request a switch to another action (e.g. volume copy -> gizmo on the new layer)
	if(state.currentAction && state.currentAction->nextAction){
		shared_ptr<InputAction> next = state.currentAction->nextAction;
		state.currentAction->nextAction = nullptr;
		setAction(next);
	}

	GLRenderer::camera->view = inverse(Runtime::controls->world);
	GLRenderer::camera->world = Runtime::controls->world;
}