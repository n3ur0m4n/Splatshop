
#pragma once

#include "SplatEditor.h"
#ifdef NO_OPENVR
	#include "NOpenVRHelper.h"
#else
	#include "OpenVRHelper.h"
#endif

// An undoable action
struct Action{
	virtual void undo() = 0;
	virtual void redo() = 0;
	virtual void release(){};
	virtual int64_t byteSize(){return 0;};
	
	Action(){

	}

	~Action(){
		release();
	}
};

// User Interaction. Handles, for example, a brushing or painting procedure from start to finish.
// A single session/instance may produce multiple undoable actions.
// For example:
// - When clicking the paint action icon, a new SpherePaintAction, subclassed from InputAction, is created.
// - Whenever the user clicks the left mouse button, a new undoable action is created.
// - When the left mouse button is released, the undoable action is finalized and registered with the editor.
// - After canceling the InputAction (e.g., right click), the last undoable is registered and the input action is removed.
struct InputAction{

	InputAction() {

	}

	virtual void start(){};
	virtual void update(){};
	virtual void stop(){};
	virtual void makeToolbarSettings(){};

	// If true, the camera can still be navigated while this action is active (left: orbit, right: pan, wheel: zoom),
	// unless the mouse is interacting with a gizmo. A right click without moving, or Esc, ends the action.
	virtual bool allowsNavigation(){ return false; };
	// If true, a left press is consumed by the action (no camera orbit while the button is held).
	virtual bool capturesLeftPress(){ return false; };

	// An action can request a switch to another action. Applied by the input handling after update().
	shared_ptr<InputAction> nextAction = nullptr;
};


