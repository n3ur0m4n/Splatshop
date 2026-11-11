
void makeTools(){

	auto editor = SplatEditor::instance;
	auto& settings = editor->settings;
	auto& state = editor->state;
	auto& scene = editor->scene;

	if (ImGui::CollapsingHeader("Tools", ImGuiTreeNodeFlags_DefaultOpen)) {

		int brushmode = int(settings.brush.mode);
		ImGui::Text("Brush Mode: "); ImGui::SameLine();
		ImGui::RadioButton("Select", &brushmode, int(BRUSHMODE::SELECT)); ImGui::SameLine();
		ImGui::RadioButton("Delete", &brushmode, int(BRUSHMODE::ERASE)); ImGui::SameLine();
		ImGui::RadioButton("Restore", &brushmode, int(BRUSHMODE::REMOVE_FLAGS)); 
		settings.brush.mode = BRUSHMODE(brushmode);

		int intersectionmode = settings.brush.intersectionmode;
		ImGui::Text("Brush intersects: "); ImGui::SameLine();
		ImGui::RadioButton("Center", &intersectionmode, BRUSH_INTERSECTION_CENTER); ImGui::SameLine();
		ImGui::RadioButton("Border", &intersectionmode, BRUSH_INTERSECTION_BORDER); 
		settings.brush.intersectionmode = intersectionmode;
		
		ImGui::SliderFloat("Brush Size", &settings.brush.size, 5.0f, 200.0f, "%.1f");

	}
};


void SplatEditor::makeEditingGUI() {

	auto editor = SplatEditor::instance;
	auto& settings = editor->settings;
	auto& state = editor->state;
	auto& scene = editor->scene;

	if(!editor->settings.showEditing) return;

	int width = 440;
	// int margin = 25;

	static SceneNode* previouslySelected = nullptr;
	SceneNode* currentlySelected = scene.find([](SceneNode* node) {return node->selected; });
	bool selectionChanged = previouslySelected != currentlySelected;

	if(selectionChanged){
		settings.colorCorrection = ColorCorrection();
	};
	

	ImGui::SetNextWindowPos(ImVec2(GLRenderer::width - width, 57 + 16 + 17 + 14));
	ImGui::SetNextWindowSize(ImVec2(width, GLRenderer::height - (57 + 16)));
	ImGui::SetNextWindowBgAlpha(1.0f);

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoBringToFrontOnFocus;
	static bool open = true;
	if(ImGui::Begin("Editing", &open, flags)){

		// makeTools();

		makeLayers();

		ImGui::Separator();

		// makeAdjustGUI();
		makeAssetGUI();

		ImGui::Separator();

		VolumeSelectAction::makePanel();
	}
	
	ImGui::End();

	previouslySelected = currentlySelected;

}