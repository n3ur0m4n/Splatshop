#include "gui/ImguiPage.h"
#include "gui/guivr.h"

struct ConcurrentTarget{
	RenderTarget target;
	CUstream mainstream;
	CUstream sidestream;

	CUevent cu_tilesComputed;

	shared_ptr<CudaVirtualMemory> virt_fb_depth;
	shared_ptr<CudaVirtualMemory> virt_fb_color;

	CUdeviceptr cptr_numVisibleSplats;
	CUdeviceptr cptr_numFragments;
	CUdeviceptr cptr_tiles;

	shared_ptr<CudaVirtualMemory> virt_numTilefragments_splatwise;
	shared_ptr<CudaVirtualMemory> virt_numTilefragments_splatwise_ordered;
	shared_ptr<CudaVirtualMemory> virt_tileIDs;
	shared_ptr<CudaVirtualMemory> virt_indices;
	shared_ptr<CudaVirtualMemory> virt_depth;
	shared_ptr<CudaVirtualMemory> virt_bounds;
	shared_ptr<CudaVirtualMemory> virt_ordering_splatdepth;
	shared_ptr<CudaVirtualMemory> virt_stagedata;

	uint32_t numVisibleSplats;
	uint32_t numFragments;
};

void dump_tile(ConcurrentTarget& target, uint32_t numTiles){

	auto editor = SplatEditor::instance;

	int width     = target.target.width;
	int height    = target.target.height;
	int numPixels = width * height;
	int tiles_x   = int(width + TILE_SIZE_3DGS - 1) / int(TILE_SIZE_3DGS);
	int tiles_y   = int(height + TILE_SIZE_3DGS - 1) / int(TILE_SIZE_3DGS);

	vector<Tile> tiles(numTiles);
	cuMemcpyDtoH(tiles.data(), target.cptr_tiles, sizeof(Tile) * numTiles);

	stringstream ss;
	ss << format("numTiles: {}\n", numTiles);

	// Tile largestTile;
	// int largestTileID = 0;
	// int splatsInLargest = 0;
	// for(int tileID = 0; tileID < numTiles; tileID++){
	// 	int numSplats = tiles[tileID].lastIndex - tiles[tileID].firstIndex;
	// 	if(numSplats > splatsInLargest){
	// 		largestTileID = tileID;
	// 		splatsInLargest = numSplats;
	// 		largestTile = tiles[tileID];
	// 	}
	// }

	

	// ss << format("largest tile: {}, splats: {} \n", largestTileID, splatsInLargest);

	auto dumpTile = [&](string label, int tile_x, int tile_y){
		Tile debugTile = tiles[tile_x + tile_y * tiles_x];

		int numSplats = debugTile.lastIndex - debugTile.firstIndex + 1;
		vector<StageData> stagedatas(target.virt_stagedata->comitted / sizeof(StageData));
		vector<uint32_t> splatIndices(target.virt_indices->comitted / 4);
		vector<float> depths(target.virt_depth->comitted / 4);
		vector<uint32_t> ordering(target.virt_ordering_splatdepth->comitted / 4);

		cuMemcpyDtoH(stagedatas.data(), target.virt_stagedata->cptr, target.virt_stagedata->comitted);
		cuMemcpyDtoH(splatIndices.data(), target.virt_indices->cptr, target.virt_indices->comitted);
		cuMemcpyDtoH(depths.data(), target.virt_depth->cptr, target.virt_depth->comitted);
		cuMemcpyDtoH(ordering.data(), target.virt_ordering_splatdepth->cptr, target.virt_ordering_splatdepth->comitted);

		stringstream ssPointcloud;

		auto decode_basisvector_i16vec2 = [](glm::i16vec2 encoded) -> vec2 {
			constexpr float basisvector_encoding_factor = 20.0f;

			float length = float(encoded.y) / basisvector_encoding_factor;
			float angle = float(encoded.x) / 10'000.0f;

			float x = cos(angle);
			float y = sin(angle);

			return vec2{x, y} * length;
		};

		for(int i = 0; i < numSplats; i++){
			int splatIndex = splatIndices[debugTile.firstIndex + i];
			StageData stagedata = stagedatas[splatIndex];
			// float depth = depths[ordering[splatIndex]];
			// float depth = stagedata.depth;
			float depth = 16.0f * float(i) / float(numSplats);

			vec2 imgpos = vec2(stagedata.imgPos_encoded) / 10.0f;
			uint8_t* rgba = (uint8_t*)&stagedata.color;

			float x = imgpos.x;
			float y = imgpos.y;
			float z = depth;

			vec2 basisvector1 = decode_basisvector_i16vec2(stagedata.basisvector1_encoded);
			vec2 basisvector2 = decode_basisvector_i16vec2(stagedata.basisvector2_encoded);

			ssPointcloud << format("{:.3f}, {:.3f}, {:.3f}, {}, {}, {}, {}, {}, {}, {}, {}\n", 
				x, y, z, 
				rgba[0], rgba[1], rgba[2], rgba[3],
				basisvector1.x, basisvector1.y,
				basisvector2.x, basisvector2.y
				);
		}

		string filename = format("dump_{}_tile_{}_{}.csv", label, tile_x, tile_y);
		writeFile(filename.c_str(), ssPointcloud.str());
	};

	for(int tx = -1; tx <= 1; tx++)
	for(int ty = -1; ty <= 1; ty++)
	{
		dumpTile("garden", 60 + tx, 50 + ty);
	}
	


	for(int tileID = 0; tileID < numTiles; tileID++){
		Tile tile = tiles[tileID];
		int tile_x = tileID % tiles_x;
		int tile_y = tileID / tiles_x;

		ss << format("Tile {:3} / {:3}, first: {:8}, last: {:8}, count: {:6}\n", 
			tile_x, tile_y,
			tile.firstIndex, tile.lastIndex, tile.lastIndex - tile.firstIndex
		);
	}

	writeFile("./dump.txt", ss.str());


	editor->settings.requestDebugDump = false;
}


void drawsplats_perspectiveCorrect_concurrent(
	Scene* scene, 
	vector<ConcurrentTarget> targets
){

	auto editor = SplatEditor::instance;
	auto& settings = editor->settings;
	auto& state = editor->state;

	static CUevent event_start = 0;
	static CUevent event_end = 0;
	static double t_start;
	static bool initialized = false;
	if(!initialized){
		cuEventCreate(&event_start, CU_EVENT_DEFAULT);
		cuEventCreate(&event_end, CU_EVENT_DEFAULT);
		initialized = true;
	}

	if(Runtime::measureTimings){
		cuCtxSynchronize();
		t_start = now();
		cuEventRecord(event_start, targets[0].mainstream);
	}

	// staging of splats needs a lot of memory, 
	// so we still do this part sequentially, one target after the other
	for(auto& target : targets){

		cuMemsetD32Async(target.cptr_numVisibleSplats  , 0, 1, target.mainstream);
		cuMemsetD32Async(target.cptr_numFragments      , 0, 1, target.mainstream);

		// hm, can we know how much we need before we need it? 
		uint32_t numPotentiallyVisibleSplats = 0;
		scene->process<SNSplats>([&](SNSplats* node) {
			if(!node->visible) return;
			numPotentiallyVisibleSplats += node->dmng.data.count;
		});
		target.virt_stagedata->commit(sizeof(StageData_perspectivecorrect) * numPotentiallyVisibleSplats);
		target.virt_bounds->commit(sizeof(glm::i16vec4) * numPotentiallyVisibleSplats);
		target.virt_ordering_splatdepth->commit(sizeof(uint32_t) * numPotentiallyVisibleSplats);
		target.virt_numTilefragments_splatwise->commit(sizeof(uint32_t) * numPotentiallyVisibleSplats);

		scene->process<SNSplats>([&](SNSplats* node) {
			if(!node->visible) return;
			if(node->dmng.data.count == 0) return;

			node->dmng.data.transform = node->transform_global;

			mat4 world = scene->world->transform;
			GaussianData data = node->dmng.data;

			ColorCorrection colorCorrection;
			if(node->selected){
				colorCorrection = settings.colorCorrection;
			}
			
			void* args[] = {
				// in
				&editor->launchArgs,
				&target, 
				&colorCorrection, 
				&node->dmng.data, 
				// out
				&target.cptr_numVisibleSplats,
				&target.cptr_numFragments, 
				&target.virt_numTilefragments_splatwise->cptr,
				&target.virt_bounds->cptr,
				&target.virt_stagedata->cptr,
				&target.virt_ordering_splatdepth->cptr,
			};
			editor->prog_gaussians_rendering->launch("kernel_stageSplats_perspectivecorrect", args, data.count, target.mainstream);
		});

		

	}
	// cuCtxSynchronize();

	// Retrieve number of staged/visible splats and tile-fragments
	int i = 0;
	for(auto& target : targets){

		// cuMemcpyDtoHAsync target must be page-locked.
		// Apparently local variables count as page-locked memory?
		uint32_t numVisibleSplats;
		uint32_t numFragments;
		CURuntime::check(cuMemcpyDtoHAsync(&numVisibleSplats, target.cptr_numVisibleSplats, 4, target.mainstream));
		CURuntime::check(cuMemcpyDtoHAsync(&numFragments, target.cptr_numFragments, 4, target.mainstream));

		cuStreamSynchronize(target.mainstream);
		cuStreamSynchronize(target.sidestream);

		Runtime::numVisibleSplats += numVisibleSplats;
		Runtime::numVisibleFragments += numFragments;

		target.numFragments = numFragments;
		target.numVisibleSplats = numVisibleSplats;

		{
			string label = "numSplats";
			string value = format(getSaneLocale(), "{:L}", numVisibleSplats);
			Runtime::debugValueList.push_back({label, value});
		}
		{
			string label = "numFragments";
			string value = format(getSaneLocale(), "{:L}", numFragments);
			Runtime::debugValueList.push_back({label, value});
		}
		
	}

	// Create tile fragment arrays concurrently
	for(auto target : targets){

		// Compute prefix sum of tile fragment counters.
		GPUPrefixSumsCS::dispatch(target.numVisibleSplats, target.virt_numTilefragments_splatwise->cptr, target.mainstream);

		target.virt_tileIDs->commit(4 * target.numFragments);
		target.virt_indices->commit(4 * target.numFragments);

		// now create the tile fragment (StageData) array
		void* argsCreatefragmentArray[] = {
			// input
			&editor->launchArgs, &target,
			&target.numVisibleSplats, &target.virt_bounds->cptr,
			&target.virt_numTilefragments_splatwise->cptr,& target.cptr_numFragments,
			// output
			&target.virt_tileIDs->cptr, &target.virt_indices->cptr,
		};

		// Lot's of syncs - without them the 16bit sorting crashed upon loadig large splat models
		cuCtxSynchronize();
		editor->prog_gaussians_rendering->launch("kernel_createTilefragmentArray_perspectivecorrect", argsCreatefragmentArray, target.numVisibleSplats, target.mainstream);
		cuCtxSynchronize();
		// TODO: shouldn't tileIDs be actual 16bit instead of 32bit uints where only the lower 16 bits are read?
		GPUSorting::sort_16bitkey_32bitvalue(target.numFragments, target.virt_tileIDs->cptr, target.virt_indices->cptr, target.mainstream);
	}

	for(auto target : targets){

		int width   = target.target.width;
		int height  = target.target.height;
		int tiles_x = (width + TILE_SIZE_PERSPCORRECT - 1) / TILE_SIZE_PERSPCORRECT;
		int tiles_y = (height + TILE_SIZE_PERSPCORRECT - 1) / TILE_SIZE_PERSPCORRECT;
		uint32_t numTiles = tiles_x * tiles_y;
		constexpr uint32_t blockSize = TILE_SIZE_PERSPCORRECT * TILE_SIZE_PERSPCORRECT;
		int tileSize = TILE_SIZE_PERSPCORRECT;

		CURuntime::check(cuMemsetD8Async(target.cptr_tiles, 0, sizeof(Tile) * numTiles, target.mainstream));
		editor->prog_gaussians_rendering->launch(
			"kernel_computeTiles_method1", 
			{&editor->launchArgs, &target, &target.virt_tileIDs->cptr, &target.numFragments, &tileSize, &target.cptr_tiles}, 
			target.numFragments, target.mainstream
		);

		cuEventRecord(target.cu_tilesComputed, target.mainstream);

		// both streams use the computed tiles, so make sure the sidestream also waits until the mainstream computed the tiles.
		cuStreamWaitEvent(target.mainstream, target.cu_tilesComputed, 0);
		cuStreamWaitEvent(target.sidestream, target.cu_tilesComputed, 0);

		if(target.numFragments > 0){

			void* args_rendering[] = {
				&editor->launchArgs, &target.target, &target.cptr_tiles, &target.virt_indices->cptr, 
				&target.virt_stagedata->cptr,
			};

			editor->prog_gaussians_rendering->launch("kernel_render_gaussians_perspectivecorrect", args_rendering, {.gridsize = numTiles, .blocksize = blockSize, .stream = target.sidestream});

		}
	}

	// wait for all streams
	for(auto target : targets){
		cuStreamSynchronize(target.mainstream);
		cuStreamSynchronize(target.sidestream);
	}
	
	if(Runtime::measureTimings){
		cuCtxSynchronize();

		double seconds = now() - t_start;

		// float duration;
		// cuEventElapsedTime(&duration, event_start, event_end);

		Runtime::timings.add("[draw splats (host)]", seconds * 1000.0f);
	}
	
}

void drawsplats_3dgs_concurrent(
	Scene* scene, 
	vector<ConcurrentTarget> targets
){

	auto editor = SplatEditor::instance;
	auto& settings = editor->settings;
	auto& state = editor->state;

	static CUevent event_start = 0;
	static CUevent event_end = 0;
	static double t_start;
	static bool initialized = false;
	if(!initialized){
		cuEventCreate(&event_start, CU_EVENT_DEFAULT);
		cuEventCreate(&event_end, CU_EVENT_DEFAULT);
		initialized = true;
	}

	if(Runtime::measureTimings){
		cuCtxSynchronize();
		t_start = now();
		cuEventRecord(event_start, targets[0].mainstream);
	}

	// staging of splats needs a lot of memory, 
	// so we still do this part sequentially, one target after the other
	for(auto& target : targets){

		cuMemsetD32Async(target.cptr_numVisibleSplats  , 0, 1, target.mainstream);
		cuMemsetD32Async(target.cptr_numFragments      , 0, 1, target.mainstream);

		// hm, can we know how much we need before we need it? 
		uint32_t numPotentiallyVisibleSplats = 0;
		scene->process<SNSplats>([&](SNSplats* node) {
			if(!node->visible) return;
			numPotentiallyVisibleSplats += node->dmng.data.count;
		});
		target.virt_stagedata->commit(sizeof(StageData) * numPotentiallyVisibleSplats);
		target.virt_depth->commit(sizeof(float) * numPotentiallyVisibleSplats);
		target.virt_ordering_splatdepth->commit(sizeof(uint32_t) * numPotentiallyVisibleSplats);
		target.virt_numTilefragments_splatwise->commit(sizeof(uint32_t) * numPotentiallyVisibleSplats);

		scene->process<SNSplats>([&](SNSplats* node) {
			if(!node->visible) return;
			if(node->dmng.data.count == 0) return;

			node->dmng.data.transform = node->transform_global;

			mat4 world = scene->world->transform;
			GaussianData data = node->dmng.data;

			ColorCorrection colorCorrection;
			if(node->selected){
				colorCorrection = settings.colorCorrection;
			}
			
			void* args[] = {
				// in
				&editor->launchArgs,
				&target, 
				&colorCorrection, 
				&node->dmng.data, 
				// out
				&target.cptr_numVisibleSplats,
				&target.cptr_numFragments, 
				&target.virt_numTilefragments_splatwise->cptr,
				&target.virt_depth->cptr,
				&target.virt_stagedata->cptr,
				&target.virt_ordering_splatdepth->cptr,
			};
			editor->prog_gaussians_rendering->launch("kernel_stageSplats", args, data.count, target.mainstream);
		});

		

	}
	// cuCtxSynchronize();

	// Retrieve number of staged/visible splats and tile-fragments
	int i = 0;
	for(auto& target : targets){

		// cuMemcpyDtoHAsync target must be page-locked.
		// Apparently local variables count as page-locked memory?
		uint32_t numVisibleSplats;
		uint32_t numFragments;
		CURuntime::check(cuMemcpyDtoHAsync(&numVisibleSplats, target.cptr_numVisibleSplats, 4, target.mainstream));
		CURuntime::check(cuMemcpyDtoHAsync(&numFragments, target.cptr_numFragments, 4, target.mainstream));

		cuStreamSynchronize(target.mainstream);
		cuStreamSynchronize(target.sidestream);

		Runtime::numVisibleSplats += numVisibleSplats;
		Runtime::numVisibleFragments += numFragments;

		target.numFragments = numFragments;
		target.numVisibleSplats = numVisibleSplats;

		{
			string label = "numSplats";
			string value = format(getSaneLocale(), "{:L}", numVisibleSplats);
			Runtime::debugValueList.push_back({label, value});
		}
		{
			string label = "numFragments";
			string value = format(getSaneLocale(), "{:L}", numFragments);
			Runtime::debugValueList.push_back({label, value});
		}
		
	}

	for(auto target : targets){

		target.virt_tileIDs->commit(4 * target.numFragments);
		target.virt_numTilefragments_splatwise_ordered->commit(4 * target.numVisibleSplats);
		target.virt_indices->commit(4 * target.numFragments);

		// Unfortunately we can't sort simultaneously, since both use the same alternative sort buffers
		cuCtxSynchronize();

		// sort visible splats by depth (or rather, compute the order without applying it).
		// We have to provide radix-sort with intermediate memory for sorting, which must be separate for each concurrent target.
		GPUSorting::sort_32bit_keyvalue(target.numVisibleSplats, target.virt_depth->cptr, target.virt_ordering_splatdepth->cptr, 0, 0,target.mainstream);

		// Apply the ordering. Necessary because we don't have 64 bit sorting and do it in a 32bit sort, followed by another 16 bit sort.
		// The follow-up 16 bit sort needs its keys to be sorted by the preceeding 32 bit sort.
		void* argsApply[] = {
			&target.virt_numTilefragments_splatwise->cptr, 
			&target.virt_numTilefragments_splatwise_ordered->cptr, 
			&target.virt_ordering_splatdepth->cptr, 
			&target.numVisibleSplats
		};
		editor->prog_gaussians_rendering->launch("kernel_applyOrdering_u32", argsApply, target.numVisibleSplats, target.mainstream);

		// Compute prefix sum of tile fragment counters.
		GPUPrefixSumsCS::dispatch(target.numVisibleSplats, target.virt_numTilefragments_splatwise_ordered->cptr, target.mainstream);
	}

	// Create tile fragment arrays concurrently
	for(auto target : targets){
		// The prefix sum is stored in-place in cptr_numTilefragments_ordered
		CUdeviceptr cptr_prefixsum = target.virt_numTilefragments_splatwise_ordered->cptr;

		// now create the tile fragment (StageData) array
		void* argsCreatefragmentArray[] = {
			// input
			&editor->launchArgs, &target,
			&target.virt_ordering_splatdepth->cptr, &target.numVisibleSplats, &target.virt_stagedata->cptr, 
			&cptr_prefixsum,& target.cptr_numFragments,
			// output
			&target.virt_tileIDs->cptr, &target.virt_indices->cptr,
		};

		editor->prog_gaussians_rendering->launch("kernel_createTilefragmentArray", argsCreatefragmentArray, target.numVisibleSplats, target.mainstream);
	}

	// But sort one target after the other because both use the same alternative sort buffers
	for(auto target : targets){
		cuCtxSynchronize();
		// The prefix sum is stored in-place in cptr_numTilefragments_ordered
		CUdeviceptr cptr_prefixsum = target.virt_numTilefragments_splatwise_ordered->cptr;
		GPUSorting::sort_16bitkey_32bitvalue(target.numFragments, target.virt_tileIDs->cptr, target.virt_indices->cptr, target.mainstream);
	}

	for(auto target : targets){

		int width   = target.target.width;
		int height  = target.target.height;
		int tiles_x = int(width + TILE_SIZE_3DGS - 1) / int(TILE_SIZE_3DGS);
		int tiles_y = int(height + TILE_SIZE_3DGS - 1) / int(TILE_SIZE_3DGS);
		uint32_t numTiles = tiles_x * tiles_y;
		int tileSize = TILE_SIZE_3DGS;

		cuMemsetD8Async(target.cptr_tiles, 0, sizeof(Tile) * numTiles, target.mainstream);
		editor->prog_gaussians_rendering->launch(
			"kernel_computeTiles_method1", 
			{&editor->launchArgs, &target, &target.virt_tileIDs->cptr, &target.numFragments, &tileSize, &target.cptr_tiles}, 
			target.numFragments, target.mainstream
		);

		cuEventRecord(target.cu_tilesComputed, target.mainstream);

		// both streams use the computed tiles, so make sure the sidestream also waits until the mainstream computed the tiles.
		cuStreamWaitEvent(target.mainstream, target.cu_tilesComputed, 0);
		cuStreamWaitEvent(target.sidestream, target.cu_tilesComputed, 0);

		if(editor->settings.requestDebugDump){ 
			dump_tile(target, numTiles);
		}

		if(target.numFragments > 0){

			uint32_t pointsInTileThreshold = 0;

			void* args_rendering[] = {
				&editor->launchArgs, &target.target, &target.cptr_tiles, &target.virt_indices->cptr, 
				&target.virt_stagedata->cptr, &pointsInTileThreshold
			};

			if(settings.rendermode == RENDERMODE_HEATMAP){
				pointsInTileThreshold = 1'000'000;
				editor->prog_gaussians_rendering->launch("kernel_render_heatmap", args_rendering, {.gridsize = numTiles, .blocksize = 256, .stream = target.sidestream});
			}else if(settings.showSolid){
				pointsInTileThreshold = 1'000'000;
				editor->prog_gaussians_rendering->launch("kernel_render_gaussians_solid", args_rendering, {.gridsize = numTiles, .blocksize = 256, .stream = target.sidestream});
			}else if(settings.enableSplatCulling){
				pointsInTileThreshold = 10'000;
				editor->prog_gaussians_rendering->launch("kernel_render_gaussians_with_discard", args_rendering, {.gridsize = numTiles, .blocksize = 256, .stream = target.mainstream});
				editor->prog_gaussians_rendering->launch("kernel_render_gaussians", args_rendering, {.gridsize = numTiles, .blocksize = 256, .stream = target.sidestream});
			}else{
				pointsInTileThreshold = 1'000'000;
				editor->prog_gaussians_rendering->launch("kernel_render_gaussians", args_rendering, {.gridsize = numTiles, .blocksize = 256, .stream = target.sidestream});
			}
		}
	}

	// wait for all streams
	for(auto target : targets){
		cuStreamSynchronize(target.mainstream);
		cuStreamSynchronize(target.sidestream);
	}
	
	if(Runtime::measureTimings){
		cuEventRecord(event_end, 0);

		cuCtxSynchronize();

		double seconds = now() - t_start;

		float duration;
		cuEventElapsedTime(&duration, event_start, event_end);

		Runtime::timings.add("[draw splats (host)]", seconds * 1000.0f);
		Runtime::timings.add("[draw splats (device)]", duration);
	}
	
}

void drawsplats_3dgs_concurrent_bandwidth(
	Scene* scene, 
	vector<ConcurrentTarget> targets
){
	// Memory bandwidth performance study with simplified code paths, e.g. single target, no solid mode, etc.

	auto target = targets[0];

	auto editor = SplatEditor::instance;
	auto& settings = editor->settings;
	auto& state = editor->state;

	static CUevent event_start = 0;
	static CUevent event_end = 0;
	static double t_start;
	static bool initialized = false;
	static CudaModularProgram* prog_render_bandwidth = nullptr;
	static shared_ptr<CudaVirtualMemory> virt_fluff_in = CURuntime::allocVirtual("bandwidth fluff in");
	static shared_ptr<CudaVirtualMemory> virt_fluff_out = CURuntime::allocVirtual("bandwidth fluff out");

	if(!initialized){
		cuEventCreate(&event_start, CU_EVENT_DEFAULT);
		cuEventCreate(&event_end, CU_EVENT_DEFAULT);
		prog_render_bandwidth = new CudaModularProgram({"./src/gaussians_rendering_bandwidth.cu"});
		
		// up to 50 floats for 30M splats
		virt_fluff_in->commit(30'000'000ll * 50ll * 4ll);
		virt_fluff_out->commit(30'000'000ll * 50ll * 4ll);
		cuMemsetD8(virt_fluff_in->cptr, 0, virt_fluff_in->comitted);

		initialized = true;
	}

	if(Runtime::measureTimings){
		cuCtxSynchronize();
		t_start = now();
		cuEventRecord(event_start, targets[0].mainstream);
	}

	// staging of splats needs a lot of memory, 
	// so we still do this part sequentially, one target after the other
	{

		cuMemsetD32Async(target.cptr_numVisibleSplats  , 0, 1, target.mainstream);
		cuMemsetD32Async(target.cptr_numFragments      , 0, 1, target.mainstream);

		// hm, can we know how much we need before we need it? 
		uint32_t numPotentiallyVisibleSplats = 0;
		scene->process<SNSplats>([&](SNSplats* node) {
			if(!node->visible) return;
			numPotentiallyVisibleSplats += node->dmng.data.count;
		});
		target.virt_stagedata->commit(sizeof(StageData) * numPotentiallyVisibleSplats);
		target.virt_depth->commit(sizeof(float) * numPotentiallyVisibleSplats);
		target.virt_ordering_splatdepth->commit(sizeof(uint32_t) * numPotentiallyVisibleSplats);
		target.virt_numTilefragments_splatwise->commit(sizeof(uint32_t) * numPotentiallyVisibleSplats);

		scene->process<SNSplats>([&](SNSplats* node) {
			if(!node->visible) return;
			if(node->dmng.data.count == 0) return;

			node->dmng.data.transform = node->transform_global;

			mat4 world = scene->world->transform;
			GaussianData data = node->dmng.data;

			ColorCorrection colorCorrection;
			if(node->selected){
				colorCorrection = settings.colorCorrection;
			}
			
			void* args[] = {
				// in
				&editor->launchArgs,
				&target, 
				&colorCorrection, 
				&node->dmng.data, 
				&virt_fluff_in->cptr,

				// out
				&target.cptr_numVisibleSplats,
				&target.cptr_numFragments, 
				&target.virt_numTilefragments_splatwise->cptr,
				&target.virt_depth->cptr,
				&target.virt_stagedata->cptr,
				&target.virt_ordering_splatdepth->cptr,
				&virt_fluff_out->cptr,
			};
			prog_render_bandwidth->launch("kernel_stageSplats", args, data.count, target.mainstream);
		});

		

	}
	// cuCtxSynchronize();

	// Retrieve number of staged/visible splats and tile-fragments
	int i = 0;
	for(auto& target : targets){

		// cuMemcpyDtoHAsync target must be page-locked.
		// Apparently local variables count as page-locked memory?
		uint32_t numVisibleSplats;
		uint32_t numFragments;
		CURuntime::check(cuMemcpyDtoHAsync(&numVisibleSplats, target.cptr_numVisibleSplats, 4, target.mainstream));
		CURuntime::check(cuMemcpyDtoHAsync(&numFragments, target.cptr_numFragments, 4, target.mainstream));

		cuStreamSynchronize(target.mainstream);
		cuStreamSynchronize(target.sidestream);

		Runtime::numVisibleSplats += numVisibleSplats;
		Runtime::numVisibleFragments += numFragments;

		target.numFragments = numFragments;
		target.numVisibleSplats = numVisibleSplats;

		{
			string label = "numSplats";
			string value = format(getSaneLocale(), "{:L}", numVisibleSplats);
			Runtime::debugValueList.push_back({label, value});
		}
		{
			string label = "numFragments";
			string value = format(getSaneLocale(), "{:L}", numFragments);
			Runtime::debugValueList.push_back({label, value});
		}
		
	}

	for(auto target : targets){

		target.virt_tileIDs->commit(4 * target.numFragments);
		target.virt_numTilefragments_splatwise_ordered->commit(4 * target.numFragments);
		target.virt_indices->commit(4 * target.numFragments);

		// Unfortunately we can't sort simultaneously, since both use the same alternative sort buffers
		cuCtxSynchronize();

		// sort visible splats by depth (or rather, compute the order without applying it).
		// We have to provide radix-sort with intermediate memory for sorting, which must be separate for each concurrent target.
		GPUSorting::sort_32bit_keyvalue(target.numVisibleSplats, target.virt_depth->cptr, target.virt_ordering_splatdepth->cptr, 0, 0,target.mainstream);

		// Apply the ordering. Necessary because we don't have 64 bit sorting and do it in a 32bit sort, followed by another 16 bit sort.
		// The follow-up 16 bit sort needs its keys to be sorted by the preceeding 32 bit sort.
		void* argsApply[] = {
			&target.virt_numTilefragments_splatwise->cptr, 
			&target.virt_numTilefragments_splatwise_ordered->cptr, 
			&target.virt_ordering_splatdepth->cptr, 
			&target.numVisibleSplats
		};
		editor->prog_gaussians_rendering->launch("kernel_applyOrdering_u32", argsApply, target.numVisibleSplats, target.mainstream);

		// Compute prefix sum of tile fragment counters.
		GPUPrefixSumsCS::dispatch(target.numVisibleSplats, target.virt_numTilefragments_splatwise_ordered->cptr, target.mainstream);
	}

	// Create tile fragment arrays concurrently
	for(auto target : targets){
		// The prefix sum is stored in-place in cptr_numTilefragments_ordered
		CUdeviceptr cptr_prefixsum = target.virt_numTilefragments_splatwise_ordered->cptr;

		// now create the tile fragment (StageData) array
		void* argsCreatefragmentArray[] = {
			// input
			&editor->launchArgs, &target,
			&target.virt_ordering_splatdepth->cptr, &target.numVisibleSplats, &target.virt_stagedata->cptr, 
			&cptr_prefixsum,& target.cptr_numFragments,
			// output
			&target.virt_tileIDs->cptr, &target.virt_indices->cptr,
		};

		editor->prog_gaussians_rendering->launch("kernel_createTilefragmentArray", argsCreatefragmentArray, target.numVisibleSplats, target.mainstream);
	}

	// But sort one target after the other because both use the same alternative sort buffers
	for(auto target : targets){
		cuCtxSynchronize();
		// The prefix sum is stored in-place in cptr_numTilefragments_ordered
		CUdeviceptr cptr_prefixsum = target.virt_numTilefragments_splatwise_ordered->cptr;
		GPUSorting::sort_16bitkey_32bitvalue(target.numFragments, target.virt_tileIDs->cptr, target.virt_indices->cptr, target.mainstream);
	}

	for(auto target : targets){

		int width   = target.target.width;
		int height  = target.target.height;
		int tiles_x = int(width + TILE_SIZE_3DGS - 1) / int(TILE_SIZE_3DGS);
		int tiles_y = int(height + TILE_SIZE_3DGS - 1) / int(TILE_SIZE_3DGS);
		uint32_t numTiles = tiles_x * tiles_y;
		int tileSize = TILE_SIZE_3DGS;

		cuMemsetD8Async(target.cptr_tiles, 0, sizeof(Tile) * numTiles, target.mainstream);
		editor->prog_gaussians_rendering->launch(
			"kernel_computeTiles_method1", 
			{&editor->launchArgs, &target, &target.virt_tileIDs->cptr, &target.numFragments, &tileSize, &target.cptr_tiles}, 
			target.numFragments, target.mainstream
		);

		cuEventRecord(target.cu_tilesComputed, target.mainstream);

		// both streams use the computed tiles, so make sure the sidestream also waits until the mainstream computed the tiles.
		cuStreamWaitEvent(target.mainstream, target.cu_tilesComputed, 0);
		cuStreamWaitEvent(target.sidestream, target.cu_tilesComputed, 0);

		if(editor->settings.requestDebugDump){ 
			dump_tile(target, numTiles);
		}

		if(target.numFragments > 0){

			uint32_t pointsInTileThreshold = 0;

			void* args_rendering[] = {
				&editor->launchArgs, &target.target, &target.cptr_tiles, &target.virt_indices->cptr, 
				&target.virt_stagedata->cptr, &pointsInTileThreshold
			};

			if(settings.showSolid){
				pointsInTileThreshold = 1'000'000;
				editor->prog_gaussians_rendering->launch("kernel_render_gaussians_solid", args_rendering, {.gridsize = numTiles, .blocksize = 256, .stream = target.sidestream});
			}else if(settings.enableSplatCulling){
				pointsInTileThreshold = 10'000;
				editor->prog_gaussians_rendering->launch("kernel_render_gaussians_with_discard", args_rendering, {.gridsize = numTiles, .blocksize = 256, .stream = target.mainstream});
				editor->prog_gaussians_rendering->launch("kernel_render_gaussians", args_rendering, {.gridsize = numTiles, .blocksize = 256, .stream = target.sidestream});
			}else{
				pointsInTileThreshold = 1'000'000;
				editor->prog_gaussians_rendering->launch("kernel_render_gaussians", args_rendering, {.gridsize = numTiles, .blocksize = 256, .stream = target.sidestream});
			}
		}
	}

	// wait for all streams
	for(auto target : targets){
		cuStreamSynchronize(target.mainstream);
		cuStreamSynchronize(target.sidestream);
	}
	
	if(Runtime::measureTimings){
		cuEventRecord(event_end, 0);

		cuCtxSynchronize();

		double seconds = now() - t_start;

		float duration;
		cuEventElapsedTime(&duration, event_start, event_end);

		Runtime::timings.add("[draw splats (host)]", seconds * 1000.0f);
		Runtime::timings.add("[draw splats (device)]", duration);
	}
	
}

void drawsplats_3dgs_concurrent_fragintersections(
	Scene* scene, 
	vector<ConcurrentTarget> targets
){
	// Fragment intersection study with simplified code paths, e.g. single target, no solid mode, etc.

	auto target = targets[0];

	auto editor = SplatEditor::instance;
	auto& settings = editor->settings;
	auto& state = editor->state;

	static CUevent event_start = 0;
	static CUevent event_end = 0;
	static double t_start;
	static bool initialized = false;
	static CudaModularProgram* prog_render_fragintersections = nullptr;

	if(!initialized){
		cuEventCreate(&event_start, CU_EVENT_DEFAULT);
		cuEventCreate(&event_end, CU_EVENT_DEFAULT);
		prog_render_fragintersections = new CudaModularProgram({"./src/gaussians_rendering_fragintersection_3dgs.cu"});

		initialized = true;
	}

	if(Runtime::measureTimings){
		cuCtxSynchronize();
		t_start = now();
		cuEventRecord(event_start, targets[0].mainstream);
	}

	// staging of splats needs a lot of memory, 
	// so we still do this part sequentially, one target after the other
	{

		cuMemsetD32Async(target.cptr_numVisibleSplats  , 0, 1, target.mainstream);
		cuMemsetD32Async(target.cptr_numFragments      , 0, 1, target.mainstream);

		// hm, can we know how much we need before we need it? 
		uint32_t numPotentiallyVisibleSplats = 0;
		scene->process<SNSplats>([&](SNSplats* node) {
			if(!node->visible) return;
			numPotentiallyVisibleSplats += node->dmng.data.count;
		});
		target.virt_stagedata->commit(sizeof(StageData) * numPotentiallyVisibleSplats);
		target.virt_depth->commit(sizeof(float) * numPotentiallyVisibleSplats);
		target.virt_ordering_splatdepth->commit(sizeof(uint32_t) * numPotentiallyVisibleSplats);
		target.virt_numTilefragments_splatwise->commit(sizeof(uint32_t) * numPotentiallyVisibleSplats);

		scene->process<SNSplats>([&](SNSplats* node) {
			if(!node->visible) return;
			if(node->dmng.data.count == 0) return;

			node->dmng.data.transform = node->transform_global;

			mat4 world = scene->world->transform;
			GaussianData data = node->dmng.data;

			ColorCorrection colorCorrection;
			if(node->selected){
				colorCorrection = settings.colorCorrection;
			}
			
			void* args[] = {
				// in
				&editor->launchArgs,
				&target, 
				&colorCorrection, 
				&node->dmng.data, 

				// out
				&target.cptr_numVisibleSplats,
				&target.cptr_numFragments, 
				&target.virt_numTilefragments_splatwise->cptr,
				&target.virt_depth->cptr,
				&target.virt_stagedata->cptr,
				&target.virt_ordering_splatdepth->cptr,
			};
			prog_render_fragintersections->launch("kernel_stageSplats", args, data.count, target.mainstream);
		});

		

	}
	// cuCtxSynchronize();

	// Retrieve number of staged/visible splats and tile-fragments
	int i = 0;
	for(auto& target : targets){

		// cuMemcpyDtoHAsync target must be page-locked.
		// Apparently local variables count as page-locked memory?
		uint32_t numVisibleSplats;
		uint32_t numFragments;
		CURuntime::check(cuMemcpyDtoHAsync(&numVisibleSplats, target.cptr_numVisibleSplats, 4, target.mainstream));
		CURuntime::check(cuMemcpyDtoHAsync(&numFragments, target.cptr_numFragments, 4, target.mainstream));

		cuStreamSynchronize(target.mainstream);
		cuStreamSynchronize(target.sidestream);

		Runtime::numVisibleSplats += numVisibleSplats;
		Runtime::numVisibleFragments += numFragments;

		target.numFragments = numFragments;
		target.numVisibleSplats = numVisibleSplats;

		{
			string label = "numSplats";
			string value = format(getSaneLocale(), "{:L}", numVisibleSplats);
			Runtime::debugValueList.push_back({label, value});
		}
		{
			string label = "numFragments";
			string value = format(getSaneLocale(), "{:L}", numFragments);
			Runtime::debugValueList.push_back({label, value});
		}
		
	}

	for(auto target : targets){

		target.virt_tileIDs->commit(4 * target.numFragments);
		target.virt_numTilefragments_splatwise_ordered->commit(4 * target.numFragments);
		target.virt_indices->commit(4 * target.numFragments);

		// Unfortunately we can't sort simultaneously, since both use the same alternative sort buffers
		cuCtxSynchronize();

		// sort visible splats by depth (or rather, compute the order without applying it).
		// We have to provide radix-sort with intermediate memory for sorting, which must be separate for each concurrent target.
		GPUSorting::sort_32bit_keyvalue(target.numVisibleSplats, target.virt_depth->cptr, target.virt_ordering_splatdepth->cptr, 0, 0,target.mainstream);

		// Apply the ordering. Necessary because we don't have 64 bit sorting and do it in a 32bit sort, followed by another 16 bit sort.
		// The follow-up 16 bit sort needs its keys to be sorted by the preceeding 32 bit sort.
		void* argsApply[] = {
			&target.virt_numTilefragments_splatwise->cptr, 
			&target.virt_numTilefragments_splatwise_ordered->cptr, 
			&target.virt_ordering_splatdepth->cptr, 
			&target.numVisibleSplats
		};
		editor->prog_gaussians_rendering->launch("kernel_applyOrdering_u32", argsApply, target.numVisibleSplats, target.mainstream);

		// Compute prefix sum of tile fragment counters.
		GPUPrefixSumsCS::dispatch(target.numVisibleSplats, target.virt_numTilefragments_splatwise_ordered->cptr, target.mainstream);
	}

	// Create tile fragment arrays concurrently
	for(auto target : targets){
		// The prefix sum is stored in-place in cptr_numTilefragments_ordered
		CUdeviceptr cptr_prefixsum = target.virt_numTilefragments_splatwise_ordered->cptr;

		// now create the tile fragment (StageData) array
		void* argsCreatefragmentArray[] = {
			// input
			&editor->launchArgs, &target,
			&target.virt_ordering_splatdepth->cptr, &target.numVisibleSplats, &target.virt_stagedata->cptr, 
			&cptr_prefixsum,& target.cptr_numFragments,
			// output
			&target.virt_tileIDs->cptr, &target.virt_indices->cptr,
		};

		prog_render_fragintersections->launch("kernel_createTilefragmentArray", argsCreatefragmentArray, target.numVisibleSplats, target.mainstream);
	}

	// But sort one target after the other because both use the same alternative sort buffers
	for(auto target : targets){
		cuCtxSynchronize();
		// The prefix sum is stored in-place in cptr_numTilefragments_ordered
		CUdeviceptr cptr_prefixsum = target.virt_numTilefragments_splatwise_ordered->cptr;
		GPUSorting::sort_16bitkey_32bitvalue(target.numFragments, target.virt_tileIDs->cptr, target.virt_indices->cptr, target.mainstream);
	}

	for(auto target : targets){

		int width   = target.target.width;
		int height  = target.target.height;
		int tiles_x = int(width + TILE_SIZE_3DGS - 1) / int(TILE_SIZE_3DGS);
		int tiles_y = int(height + TILE_SIZE_3DGS - 1) / int(TILE_SIZE_3DGS);
		uint32_t numTiles = tiles_x * tiles_y;
		int tileSize = TILE_SIZE_3DGS;

		cuMemsetD8Async(target.cptr_tiles, 0, sizeof(Tile) * numTiles, target.mainstream);
		editor->prog_gaussians_rendering->launch(
			"kernel_computeTiles_method1", 
			{&editor->launchArgs, &target, &target.virt_tileIDs->cptr, &target.numFragments, &tileSize, &target.cptr_tiles}, 
			target.numFragments, target.mainstream
		);

		cuEventRecord(target.cu_tilesComputed, target.mainstream);

		// both streams use the computed tiles, so make sure the sidestream also waits until the mainstream computed the tiles.
		cuStreamWaitEvent(target.mainstream, target.cu_tilesComputed, 0);
		cuStreamWaitEvent(target.sidestream, target.cu_tilesComputed, 0);

		if(editor->settings.requestDebugDump){ 
			dump_tile(target, numTiles);
		}

		if(target.numFragments > 0){

			uint32_t pointsInTileThreshold = 0;

			void* args_rendering[] = {
				&editor->launchArgs, &target.target, &target.cptr_tiles, &target.virt_indices->cptr, 
				&target.virt_stagedata->cptr, &pointsInTileThreshold
			};

			if(settings.rendermode == RENDERMODE_HEATMAP){
				pointsInTileThreshold = 1'000'000;
				editor->prog_gaussians_rendering->launch("kernel_render_heatmap", args_rendering, {.gridsize = numTiles, .blocksize = 256, .stream = target.sidestream});
			}else if(settings.showSolid){
				pointsInTileThreshold = 1'000'000;
				editor->prog_gaussians_rendering->launch("kernel_render_gaussians_solid", args_rendering, {.gridsize = numTiles, .blocksize = 256, .stream = target.sidestream});
			}else if(settings.enableSplatCulling){
				pointsInTileThreshold = 10'000;
				editor->prog_gaussians_rendering->launch("kernel_render_gaussians_with_discard", args_rendering, {.gridsize = numTiles, .blocksize = 256, .stream = target.mainstream});
				editor->prog_gaussians_rendering->launch("kernel_render_gaussians", args_rendering, {.gridsize = numTiles, .blocksize = 256, .stream = target.sidestream});
			}else{
				pointsInTileThreshold = 1'000'000;
				editor->prog_gaussians_rendering->launch("kernel_render_gaussians", args_rendering, {.gridsize = numTiles, .blocksize = 256, .stream = target.sidestream});
			}

			
		}
	}

	// wait for all streams
	for(auto target : targets){
		cuStreamSynchronize(target.mainstream);
		cuStreamSynchronize(target.sidestream);
	}
	
	if(Runtime::measureTimings){
		cuEventRecord(event_end, 0);

		cuCtxSynchronize();

		double seconds = now() - t_start;

		float duration;
		cuEventElapsedTime(&duration, event_start, event_end);

		Runtime::timings.add("[draw splats (host)]", seconds * 1000.0f);
		Runtime::timings.add("[draw splats (device)]", duration);
	}
	
}

void drawsplats_3dgs_concurrent_soa(
	Scene* scene, 
	vector<ConcurrentTarget> targets
){
	// Structure-of-Array performance study with simplified code paths, e.g. single target, no solid mode, etc.

	auto target = targets[0];

	auto editor = SplatEditor::instance;
	auto& settings = editor->settings;
	auto& state = editor->state;

	static CUevent event_start = 0;
	static CUevent event_end = 0;
	static double t_start;
	static bool initialized = false;
	static CudaModularProgram* prog_render_soa = nullptr;
	static shared_ptr<CudaVirtualMemory> virt_sd_basisvector1_encoded = CURuntime::allocVirtual("sd_basisvector1_encoded");
	static shared_ptr<CudaVirtualMemory> virt_sd_basisvector2_encoded = CURuntime::allocVirtual("sd_basisvector2_encoded");
	static shared_ptr<CudaVirtualMemory> virt_sd_imgPos_encoded = CURuntime::allocVirtual("sd_imgPos_encoded");
	static shared_ptr<CudaVirtualMemory> virt_sd_color = CURuntime::allocVirtual("sd_color");
	static shared_ptr<CudaVirtualMemory> virt_sd_flags = CURuntime::allocVirtual("sd_flags");
	static shared_ptr<CudaVirtualMemory> virt_sd_depth = CURuntime::allocVirtual("sd_depth");

	if(!initialized){
		cuEventCreate(&event_start, CU_EVENT_DEFAULT);
		cuEventCreate(&event_end, CU_EVENT_DEFAULT);

		prog_render_soa = new CudaModularProgram({"./src/gaussians_rendering_soa.cu"});
		initialized = true;
	}

	if(Runtime::measureTimings){
		cuCtxSynchronize();
		t_start = now();
		cuEventRecord(event_start, targets[0].mainstream);
	}

	// staging of splats needs a lot of memory, 
	// so we still do this part sequentially, one target after the other
	{

		cuMemsetD32Async(target.cptr_numVisibleSplats  , 0, 1, target.mainstream);
		cuMemsetD32Async(target.cptr_numFragments      , 0, 1, target.mainstream);

		// hm, can we know how much we need before we need it? 
		uint32_t numPotentiallyVisibleSplats = 0;
		scene->process<SNSplats>([&](SNSplats* node) {
			if(!node->visible) return;
			numPotentiallyVisibleSplats += node->dmng.data.count;
		});
		
		// target.virt_stagedata->commit(sizeof(StageData) * numPotentiallyVisibleSplats);
		virt_sd_basisvector1_encoded->commit(sizeof(glm::i16vec2) * numPotentiallyVisibleSplats);
		virt_sd_basisvector2_encoded->commit(sizeof(glm::i16vec2) * numPotentiallyVisibleSplats);
		virt_sd_imgPos_encoded->commit(sizeof(glm::i16vec2) * numPotentiallyVisibleSplats);
		virt_sd_color->commit(sizeof(uint32_t) * numPotentiallyVisibleSplats);
		virt_sd_flags->commit(sizeof(uint32_t) * numPotentiallyVisibleSplats);
		virt_sd_depth->commit(sizeof(float) * numPotentiallyVisibleSplats);

		target.virt_depth->commit(sizeof(float) * numPotentiallyVisibleSplats);
		target.virt_ordering_splatdepth->commit(sizeof(uint32_t) * numPotentiallyVisibleSplats);
		target.virt_numTilefragments_splatwise->commit(sizeof(uint32_t) * numPotentiallyVisibleSplats);

		scene->process<SNSplats>([&](SNSplats* node) {
			if(!node->visible) return;
			if(node->dmng.data.count == 0) return;

			node->dmng.data.transform = node->transform_global;

			mat4 world = scene->world->transform;
			GaussianData data = node->dmng.data;

			ColorCorrection colorCorrection;
			if(node->selected){
				colorCorrection = settings.colorCorrection;
			}
			
			void* args[] = {
				// in
				&editor->launchArgs,
				&target, 
				&colorCorrection, 
				&node->dmng.data, 
				// out
				&target.cptr_numVisibleSplats,
				&target.cptr_numFragments, 
				&target.virt_numTilefragments_splatwise->cptr,
				&target.virt_depth->cptr,

				// &target.virt_stagedata->cptr,
				&virt_sd_basisvector1_encoded->cptr,
				&virt_sd_basisvector2_encoded->cptr,
				&virt_sd_imgPos_encoded->cptr,
				&virt_sd_color->cptr,
				&virt_sd_flags->cptr,
				&virt_sd_depth->cptr,

				&target.virt_ordering_splatdepth->cptr,
			};
			
			prog_render_soa->launch("kernel_stageSplats", args, data.count, target.mainstream);
		});

		

	}
	// cuCtxSynchronize();

	// Retrieve number of staged/visible splats and tile-fragments
	int i = 0;
	{

		// cuMemcpyDtoHAsync target must be page-locked.
		// Apparently local variables count as page-locked memory?
		uint32_t numVisibleSplats;
		uint32_t numFragments;
		CURuntime::check(cuMemcpyDtoHAsync(&numVisibleSplats, target.cptr_numVisibleSplats, 4, target.mainstream));
		CURuntime::check(cuMemcpyDtoHAsync(&numFragments, target.cptr_numFragments, 4, target.mainstream));

		cuStreamSynchronize(target.mainstream);
		cuStreamSynchronize(target.sidestream);

		Runtime::numVisibleSplats += numVisibleSplats;
		Runtime::numVisibleFragments += numFragments;

		target.numFragments = numFragments;
		target.numVisibleSplats = numVisibleSplats;

		{
			string label = "numSplats";
			string value = format(getSaneLocale(), "{:L}", numVisibleSplats);
			Runtime::debugValueList.push_back({label, value});
		}
		{
			string label = "numFragments";
			string value = format(getSaneLocale(), "{:L}", numFragments);
			Runtime::debugValueList.push_back({label, value});
		}
		
	}

	{

		target.virt_tileIDs->commit(4 * target.numFragments);
		target.virt_numTilefragments_splatwise_ordered->commit(4 * target.numFragments);
		target.virt_indices->commit(4 * target.numFragments);

		// Unfortunately we can't sort simultaneously, since both use the same alternative sort buffers
		cuCtxSynchronize();

		// sort visible splats by depth (or rather, compute the order without applying it).
		// We have to provide radix-sort with intermediate memory for sorting, which must be separate for each concurrent target.
		GPUSorting::sort_32bit_keyvalue(target.numVisibleSplats, target.virt_depth->cptr, target.virt_ordering_splatdepth->cptr, 0, 0,target.mainstream);

		// Apply the ordering. Necessary because we don't have 64 bit sorting and do it in a 32bit sort, followed by another 16 bit sort.
		// The follow-up 16 bit sort needs its keys to be sorted by the preceeding 32 bit sort.
		void* argsApply[] = {
			&target.virt_numTilefragments_splatwise->cptr, 
			&target.virt_numTilefragments_splatwise_ordered->cptr, 
			&target.virt_ordering_splatdepth->cptr, 
			&target.numVisibleSplats
		};
		editor->prog_gaussians_rendering->launch("kernel_applyOrdering_u32", argsApply, target.numVisibleSplats, target.mainstream);

		// Compute prefix sum of tile fragment counters.
		GPUPrefixSumsCS::dispatch(target.numVisibleSplats, target.virt_numTilefragments_splatwise_ordered->cptr, target.mainstream);
	}

	// Create tile fragment arrays concurrently
	{
		// The prefix sum is stored in-place in cptr_numTilefragments_ordered
		CUdeviceptr cptr_prefixsum = target.virt_numTilefragments_splatwise_ordered->cptr;

		// now create the tile fragment (StageData) array
		void* argsCreatefragmentArray[] = {
			// input
			&editor->launchArgs, &target,
			&target.virt_ordering_splatdepth->cptr, &target.numVisibleSplats, 
			
			//&target.virt_stagedata->cptr, 
			&virt_sd_basisvector1_encoded->cptr,
			&virt_sd_basisvector2_encoded->cptr,
			&virt_sd_imgPos_encoded->cptr,
			&virt_sd_color->cptr,
			&virt_sd_flags->cptr,
			&virt_sd_depth->cptr,

			&cptr_prefixsum,& target.cptr_numFragments,
			// output
			&target.virt_tileIDs->cptr, &target.virt_indices->cptr,
		};

		prog_render_soa->launch("kernel_createTilefragmentArray", argsCreatefragmentArray, target.numVisibleSplats, target.mainstream);
	}

	// But sort one target after the other because both use the same alternative sort buffers
	{
		cuCtxSynchronize();
		// The prefix sum is stored in-place in cptr_numTilefragments_ordered
		CUdeviceptr cptr_prefixsum = target.virt_numTilefragments_splatwise_ordered->cptr;
		GPUSorting::sort_16bitkey_32bitvalue(target.numFragments, target.virt_tileIDs->cptr, target.virt_indices->cptr, target.mainstream);
	}

	{

		int width   = target.target.width;
		int height  = target.target.height;
		int tiles_x = int(width + TILE_SIZE_3DGS - 1) / int(TILE_SIZE_3DGS);
		int tiles_y = int(height + TILE_SIZE_3DGS - 1) / int(TILE_SIZE_3DGS);
		uint32_t numTiles = tiles_x * tiles_y;
		int tileSize = TILE_SIZE_3DGS;

		cuMemsetD8Async(target.cptr_tiles, 0, sizeof(Tile) * numTiles, target.mainstream);
		editor->prog_gaussians_rendering->launch(
			"kernel_computeTiles_method1", 
			{&editor->launchArgs, &target, &target.virt_tileIDs->cptr, &target.numFragments, &tileSize, &target.cptr_tiles}, 
			target.numFragments, target.mainstream
		);

		cuEventRecord(target.cu_tilesComputed, target.mainstream);

		// both streams use the computed tiles, so make sure the sidestream also waits until the mainstream computed the tiles.
		cuStreamWaitEvent(target.mainstream, target.cu_tilesComputed, 0);
		cuStreamWaitEvent(target.sidestream, target.cu_tilesComputed, 0);

		if(target.numFragments > 0){

			uint32_t pointsInTileThreshold = 1'000'000;

			void* args_rendering[] = {
				&editor->launchArgs, &target.target, &target.cptr_tiles, &target.virt_indices->cptr, 
				
				//&target.virt_stagedata->cptr, 
				&virt_sd_basisvector1_encoded->cptr,
				&virt_sd_basisvector2_encoded->cptr,
				&virt_sd_imgPos_encoded->cptr,
				&virt_sd_color->cptr,
				&virt_sd_flags->cptr,
				&virt_sd_depth->cptr,
				
				&pointsInTileThreshold
			};

			

			prog_render_soa->launch("kernel_render_gaussians", args_rendering, {.gridsize = numTiles, .blocksize = 256, .stream = target.sidestream});
		}
	}

	// wait for all streams
	{
		cuStreamSynchronize(target.mainstream);
		cuStreamSynchronize(target.sidestream);
	}
	
	if(Runtime::measureTimings){
		cuEventRecord(event_end, 0);

		cuCtxSynchronize();

		double seconds = now() - t_start;

		float duration;
		cuEventElapsedTime(&duration, event_start, event_end);

		Runtime::timings.add("[draw splats (host)]", seconds * 1000.0f);
		Runtime::timings.add("[draw splats (device)]", duration);
	}
	
}


void SplatEditor::draw(Scene* scene, vector<RenderTarget> targets){

	cuCtxSynchronize();

	// Stuff that only needs to be done once for all targets
	vector<SNPoints*> nodes;
	scene->process<SNPoints>([&](SNPoints* node){
		if(!node->visible) return;
		nodes.push_back(node);
	});

	scene->forEach<SNTriangles>([&](SNTriangles* node) {
		if (!node->visible) return;

		node->data.transform = node->transform_global;

		Runtime::numRenderedTriangles += node->data.count;

		TriangleQueueItem item;
		item.geometry = node->data;
		item.material = node->material;

		triangleQueue.push_back(item);
	});

	// Cache concurrency stuff that needs to be dedicated to each target.
	static vector<ConcurrentTarget> cache;
	while(cache.size() < targets.size()){
		CUstream mainstream, sidestream;
		CURuntime::check(cuStreamCreate(&mainstream, CU_STREAM_NON_BLOCKING));
		CURuntime::check(cuStreamCreate(&sidestream, CU_STREAM_NON_BLOCKING));

		CUevent cu_tilesComputed;
		cuEventCreate(&cu_tilesComputed, CU_EVENT_DEFAULT);

		int MAX_WIDTH = 4096;
		int MAX_HEIGHT = 4096;
		int tileSize = 8; // use smallest potential tile size to allocate enough memory for number of tiles
		int MAX_TILES_X = MAX_WIDTH / tileSize;
		int MAX_TILES_Y = MAX_HEIGHT / tileSize;

		ConcurrentTarget concurrent;
		concurrent.mainstream = mainstream;
		concurrent.sidestream = sidestream;
		concurrent.cu_tilesComputed = cu_tilesComputed;
		concurrent.virt_fb_depth = CURuntime::allocVirtual("fb_depth");
		concurrent.virt_fb_color = CURuntime::allocVirtual("fb_color");

		concurrent.cptr_numVisibleSplats           = CURuntime::alloc("numVisibleSplats", 8);
		concurrent.cptr_numFragments               = CURuntime::alloc("numFragments", 8);
		concurrent.cptr_tiles                      = CURuntime::alloc("tiles", sizeof(Tile) * MAX_TILES_X * MAX_TILES_Y);
		
		concurrent.virt_tileIDs                            = CURuntime::allocVirtual("sm1_tileIDs");
		concurrent.virt_indices                            = CURuntime::allocVirtual("sm1_indices");
		concurrent.virt_depth                              = CURuntime::allocVirtual("sm1_depth");
		concurrent.virt_bounds                             = CURuntime::allocVirtual("sm1_bounds");
		concurrent.virt_stagedata                          = CURuntime::allocVirtual("sm1_stagedata");
		concurrent.virt_ordering_splatdepth                = CURuntime::allocVirtual("sm1_ordering_splatdepth ");
		concurrent.virt_numTilefragments_splatwise         = CURuntime::allocVirtual("sm1_numTilefragments_splatwise");
		concurrent.virt_numTilefragments_splatwise_ordered = CURuntime::allocVirtual("sm1_numTilefragments_splatwise_ordered");

		cache.push_back(concurrent);
	}

	// Augment given targets by (cached) concurrent auxiliary stuff
	vector<ConcurrentTarget> concurrentTargets;
	for(int i = 0; i < targets.size(); i++){

		ConcurrentTarget concurrent = cache[i];
		concurrent.target = targets[i];

		concurrentTargets.push_back(concurrent);
	}

	if(ovr->isActive()){
		makeAssetsVR(imn_assets->page);
		makeBrushesVR(imn_brushes->page);
		makeLayersVR(imn_layers->page);
		makePaintingVR(imn_painting->page);
	}

	// NOTE: triangle&point rendering is actually still sequential. 
	// Currently needs to be sequential due to global device counters in kernel.
	// TODO: Make triangle and point rendering concurrent for all targets. 
	for(ConcurrentTarget concurrent : concurrentTargets){

		RenderTarget target = concurrent.target;
		CUstream mainstream = concurrent.mainstream;
		CUstream sidestream = concurrent.sidestream;

		prog_gaussians_rendering->launch("kernel_clearFramebuffer", {&launchArgs, &target}, target.width * target.height, mainstream);

		if(nodes.size() > 0)
		{ // RENDER POINTS - HQ

			shared_ptr<CudaVirtualMemory> virt_fb_depth = concurrent.virt_fb_depth;
			shared_ptr<CudaVirtualMemory> virt_fb_color = concurrent.virt_fb_color;

			virt_fb_depth->commit(target.width * target.height * 4);
			virt_fb_color->commit(target.width * target.height * 16);

			uint32_t INF = 0x7f800000;
			cuMemsetD32Async(virt_fb_depth->cptr, INF, target.width * target.height, mainstream);
			cuMemsetD32Async(virt_fb_color->cptr, 0, 4 * target.width * target.height, mainstream);

			float pointSize = 0.5f;

			// depthmap
			for(SNPoints* node : nodes){
				prog_points->launch("kernel_hqs_depth", {&launchArgs, &node->manager.data, &target, &virt_fb_depth->cptr, &virt_fb_color->cptr, &pointSize}, node->manager.data.count, mainstream);
			}

			// colors
			for(SNPoints* node : nodes){
				prog_points->launch("kernel_hqs_color", {&launchArgs, &node->manager.data, &target, &virt_fb_depth->cptr, &virt_fb_color->cptr, &pointSize}, node->manager.data.count, mainstream);
			}

			// normalize and transfer to target.framebuffer
			uint32_t numPixels = target.width * target.height;
			prog_points->launch("kernel_hqs_normalize", {&launchArgs, &target, &virt_fb_depth->cptr, &virt_fb_color->cptr}, numPixels, mainstream);
		}

		{ // RENDER TRIANGLES
			
			if (triangleQueue.size() > 0)
			{
				static CUdeviceptr cptr_queue_geometry = 0;
				static CUdeviceptr cptr_queue_material = 0;

				if (cptr_queue_geometry == 0) {
					cptr_queue_geometry = CURuntime::alloc("queue_geometry", 100'000 * sizeof(TriangleData));
					cptr_queue_material = CURuntime::alloc("queue_material", 100'000 * sizeof(TriangleMaterial));
				}

				vector<TriangleData> queue_data;
				vector<TriangleMaterial> queue_material;
				for (auto item : triangleQueue) {
					queue_data.push_back(item.geometry);
					queue_material.push_back(item.material);
				}

				cuMemcpyHtoDAsync(cptr_queue_geometry, queue_data.data(), queue_data.size() * sizeof(TriangleData), mainstream);
				cuMemcpyHtoDAsync(cptr_queue_material, queue_material.data(), queue_material.size() * sizeof(TriangleMaterial), mainstream);

				TriangleModelQueue queue;
				queue.count = queue_data.size();
				queue.geometries = (TriangleData*)cptr_queue_geometry;
				queue.materials = (TriangleMaterial*)cptr_queue_material;

				OptionalLaunchSettings settings = { 0 };
				settings.blocksize = 64;
				settings.stream = mainstream;
				prog_triangles->launchCooperative("kernel_drawTriangleQueue", { &launchArgs, &queue, &target }, settings);
			}
		}

		if(ovr->isActive())
		{
			

			auto glmapping_assets = mapCudaGl(imn_assets->page->framebuffer->colorAttachments[0]);
			auto glmapping_brushes = mapCudaGl(imn_brushes->page->framebuffer->colorAttachments[0]);
			auto glmapping_layers = mapCudaGl(imn_layers->page->framebuffer->colorAttachments[0]);
			auto glmapping_painting = mapCudaGl(imn_painting->page->framebuffer->colorAttachments[0]);

			{ // DRAW VR MENU ASSETS		
				imn_assets->mesh->data.transform = imn_assets->transform;
				imn_assets->mesh->material.texture.data = nullptr;
				imn_assets->mesh->material.texture.surface = -1;
				imn_assets->mesh->material.texture.cutexture = glmapping_assets.texture;
				imn_assets->mesh->material.texture.width = imn_assets->page->width;
				imn_assets->mesh->material.texture.height = imn_assets->page->height;
				imn_assets->mesh->material.mode = MATERIAL_MODE_TEXTURED;
				prog_triangles->launchCooperative("kernel_drawTriangles", {&launchArgs, &imn_assets->mesh->data, &imn_assets->mesh->material, &target}, {.stream = mainstream});

				Runtime::numRenderedTriangles += imn_assets->mesh->data.count;
			}

			{ // DRAW VR MENU BRUSHES
				imn_brushes->mesh->data.transform = imn_brushes->transform;
				imn_brushes->mesh->material.texture.data = nullptr;
				imn_brushes->mesh->material.texture.surface = -1;
				imn_brushes->mesh->material.texture.cutexture = glmapping_brushes.texture;
				imn_brushes->mesh->material.texture.width = imn_brushes->page->width;
				imn_brushes->mesh->material.texture.height = imn_brushes->page->height;
				imn_brushes->mesh->material.mode = MATERIAL_MODE_TEXTURED;
				prog_triangles->launchCooperative("kernel_drawTriangles", {&launchArgs, &imn_brushes->mesh->data, &imn_brushes->mesh->material, &target}, {.stream = mainstream});

				Runtime::numRenderedTriangles += imn_brushes->mesh->data.count;
			}

			{ // DRAW VR MENU LAYERS
				shared_ptr<ImguiNode> node = imn_layers;

				node->mesh->data.transform = node->transform;
				node->mesh->material.texture.data = nullptr;
				node->mesh->material.texture.surface = -1;
				node->mesh->material.texture.cutexture = glmapping_layers.texture;
				node->mesh->material.texture.width = node->page->width;
				node->mesh->material.texture.height = node->page->height;
				node->mesh->material.mode = MATERIAL_MODE_TEXTURED;
				prog_triangles->launchCooperative("kernel_drawTriangles", {&launchArgs, &node->mesh->data, &node->mesh->material, &target}, {.stream = mainstream});

				Runtime::numRenderedTriangles += node->mesh->data.count;
			}

			{ // DRAW VR MENU PAINTING
				shared_ptr<ImguiNode> node = imn_painting;

				node->mesh->data.transform = node->transform;
				node->mesh->material.texture.data = nullptr;
				node->mesh->material.texture.surface = -1;
				node->mesh->material.texture.cutexture = glmapping_painting.texture;
				node->mesh->material.texture.width = node->page->width;
				node->mesh->material.texture.height = node->page->height;
				node->mesh->material.mode = MATERIAL_MODE_TEXTURED;
				prog_triangles->launchCooperative("kernel_drawTriangles", {&launchArgs, &node->mesh->data, &node->mesh->material, &target}, {.stream = mainstream});

				Runtime::numRenderedTriangles += node->mesh->data.count;
			}

			glmapping_assets.unmap();
			glmapping_brushes.unmap();
			glmapping_layers.unmap();
			glmapping_painting.unmap();
		}

		// cuCtxSynchronize();

		{ // DRAW DEVICE LINES

			static CUdeviceptr cptr_numProcessedLines_0 = CURuntime::alloc("processed lines counter", 8);
			static CUdeviceptr cptr_numProcessedLines_1 = CURuntime::alloc("processed lines counter", 8);

			cuMemsetD32Async(cptr_numProcessedLines_0, 0, 2, mainstream);
			cuMemsetD32Async(cptr_numProcessedLines_1, 0, 2, mainstream);

			OptionalLaunchSettings settings = {0};
			settings.blocksize = 64;
			settings.stream = mainstream;
			prog_lines->launchCooperative("kernel_drawLines", {&launchArgs, &target, &cptr_lines, &cptr_numLines, &cptr_numProcessedLines_0}, settings);
			prog_lines->launchCooperative("kernel_drawLines", {&launchArgs, &target, &virt_lines_host->cptr, &cptr_numLines_host, &cptr_numProcessedLines_1}, settings);
		}

		cuCtxSynchronize();

		if(settings.enableEDL){
			prog_helpers->launch("kernel_applyEyeDomeLighting", {&launchArgs, &target}, target.width * target.height, mainstream);
		}

		cuEventRecord(event_edl_applied, mainstream);
		cuStreamWaitEvent(mainstream, event_edl_applied, CU_EVENT_WAIT_DEFAULT);
		cuStreamWaitEvent(sidestream, event_edl_applied, CU_EVENT_WAIT_DEFAULT);

		if(Runtime::measureTimings){
			cuEventRecord(event_mainstream, 0);
		}
	}

	cuCtxSynchronize();

	if(settings.splatRenderer == SPLATRENDERER_3DGS){

		// if(settings.intersectionMode && INTERSECTION_3DGS || settings.intersectionMode ==INTERSECTION_TIGHTBB){
		// 	drawsplats_3dgs_concurrent_fragintersections(scene, concurrentTargets);
		// }else 
		if(settings.renderSoA){
			drawsplats_3dgs_concurrent_soa(scene, concurrentTargets);
		}else if(settings.renderBandwidth){
			drawsplats_3dgs_concurrent_bandwidth(scene, concurrentTargets);
		}else if(settings.renderFragIntersections){
			drawsplats_3dgs_concurrent_fragintersections(scene, concurrentTargets);
		}else{
			drawsplats_3dgs_concurrent(scene, concurrentTargets);
		}

	}else if(settings.splatRenderer == SPLATRENDERER_PERSPECTIVE_CORRECT){
		drawsplats_perspectiveCorrect_concurrent(scene, concurrentTargets);
	}

	{ // SELECTION VOLUMES - translucent overlay on top of the splats
		vector<VolumeGPU> volumes = VolumeSelectAction::getRenderVolumes();

		if(volumes.size() > 0){
			cuCtxSynchronize();

			CUdeviceptr cptr_volumes = VolumeSelectAction::uploadVolumes(volumes);
			int numVolumes = volumes.size();

			for(ConcurrentTarget& concurrent : concurrentTargets){
				RenderTarget target = concurrent.target;
				mat4 invView = inverse(target.view);

				prog_helpers->launch("kernel_drawVolumes", {&launchArgs, &target, &invView, &cptr_volumes, &numVolumes}, target.width * target.height);
			}

			cuCtxSynchronize();
		}
	}
}