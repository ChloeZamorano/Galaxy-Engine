#include <iostream>
#include <vector>
#include <cmath>
#include <array>
#include <omp.h>
#include <thread>
#include <algorithm>
#include <bitset>

#include "../include/Particles/particle.h"
#include "../include/Physics/quadtree.h"
#include "../include/Physics/slingshot.h"
#include "../include/Particles/particleTrails.h"
#include "../include/UI/button.h"
#include "../include/UX/screenCapture.h"
#include "../include/Physics/morton.h"
#include "../include/UI/slider.h"
#include "../include/UX/camera.h"
#include "../include/raylib/raylib.h"
#include "../include/raylib/rlgl.h"
#include "../include/raylib/raymath.h"
#include "../include/UI/brush.h"
#include "../include/Particles/particleSelection.h"
#include "../include/Particles/particleSubdivision.h"
#include "../include/Particles/densitySize.h"
#include "../include/Particles/particleColorVisuals.h"
#include "../include/UI/rightClickSettings.h"
#include "../include/UI/controls.h"
#include "../include/Particles/particleDeletion.h"
#include "../include/Particles/particlesSpawning.h"
#include "../include/UI/UI.h"
#include "../include/Physics/physics.h"
#include "../include/Physics/collisionGrid.h"
#include "../include/parameters.h"
#include "../include/Particles/particleSpaceship.h"
#include "../include/Physics/SPH.h"


UpdateParameters myParam;
UpdateVariables myVar;
UI myUI;
Physics physics;
CollisionGrid collisionGrid;
ParticleSpaceship ship;
SPH sph;


static Quadtree* gridFunction(std::vector<ParticlePhysics>& pParticles,
	std::vector<ParticleRendering>& rParticles) {
	Quadtree* grid = Quadtree::boundingBox(pParticles, rParticles);
	return grid;
}


void flattenQuadtree(Quadtree* node, std::vector<Quadtree*>& flatList) {
	if (!node) return;

	flatList.push_back(node);

	for (const auto& child : node->subGrids) {
		flattenQuadtree(child.get(), flatList);
	}
}

struct ParticleBounds {
	float minX, maxX, minY, maxY;
};

static void updateScene() {

	Quadtree* grid = nullptr;

	myVar.G = 6.674e-11 * myVar.gravityMultiplier;

	if (IsKeyPressed(KEY_SPACE)) {
		myVar.isTimePlaying = !myVar.isTimePlaying;
	}

	myVar.timeFactor = myVar.fixedDeltaTime * myVar.timeStepMultiplier * static_cast<float>(myVar.isTimePlaying);

	//if (myVar.timeFactor == 0) {
	//	myParam.morton.computeMortonKeys(myParam.pParticles, grid->boundingBoxPos, grid->boundingBoxSize);
	//	myParam.morton.sortParticlesByMortonKey(myParam.pParticles, myParam.rParticles);
	//}

	if (myVar.timeFactor > 0) {
		grid = gridFunction(myParam.pParticles, myParam.rParticles);
	}

	/*std::vector<Quadtree*> flatNodes;

	flattenQuadtree(grid, flatNodes);

	size_t index = 0;*/

	if (grid != nullptr && myVar.drawQuadtree) {
		grid->drawQuadtree();
	}


	for (ParticleRendering& rParticle : myParam.rParticles) {
		rParticle.totalRadius = rParticle.size * myVar.particleTextureHalfSize * myVar.particleSizeMultiplier;
	}

	myParam.brush.brushSize();

	myParam.particlesSpawning.particlesInitialConditions(grid, physics, myVar, myParam);

	myParam.particlesSpawning.copyPaste(myParam.pParticles, myParam.rParticles, myVar.isDragging, myParam.myCamera, myParam.pParticlesSelected);

	if (myVar.timeFactor > 0.0f && grid != nullptr) {

		for (size_t i = 0; i < myParam.pParticles.size(); i++) {
			myParam.pParticles[i].acc = { 0.0f, 0.0f };
		}

		if (myVar.isSPHEnabled) {
			sph.Solver(myParam.pParticles, myParam.rParticles, myVar.timeFactor, myVar.domainSize);
		}

#pragma omp parallel for schedule(dynamic)
		for (size_t i = 0; i < myParam.pParticles.size(); i++) {

			ParticlePhysics& pParticle = myParam.pParticles[i];

			Vector2 netForce = physics.calculateForceFromGrid(*grid, myParam.pParticles, myVar, pParticle);

			pParticle.acc.x += netForce.x / pParticle.mass;
			pParticle.acc.y += netForce.y / pParticle.mass;
		}

		ship.spaceshipLogic(myParam.pParticles, myParam.rParticles, myVar.isShipGasEnabled);

		if (myVar.isCollisionsEnabled) {
			float dt = myVar.timeFactor / myVar.substeps;

			for (int i = 0; i < myVar.substeps; ++i) {
				collisionGrid.buildGrid(myParam.pParticles, myParam.rParticles, physics, myVar, myVar.domainSize, dt);
			}
		}

		physics.physicsUpdate(myParam.pParticles, myParam.rParticles, myVar);
	}

	if (myVar.isDensitySizeEnabled || myParam.colorVisuals.densityColor) {
		myParam.neighborSearch.neighborSearch(myParam.pParticles, myParam.rParticles, myVar.particleSizeMultiplier, myVar.particleTextureHalfSize);
	}

	myParam.trails.trailLogic(myVar, myParam);

	myParam.myCamera.cameraFollowObject(myVar, myParam);

	myParam.particleSelection.clusterSelection(myVar, myParam);

	myParam.particleSelection.particleSelection(myVar, myParam);

	myParam.particleSelection.manyClustersSelection(myVar, myParam);

	myParam.particleSelection.boxSelection(myParam);

	myParam.particleSelection.invertSelection(myParam.rParticles);

	myParam.particleSelection.deselection(myParam.rParticles);

	myParam.particleSelection.selectedParticlesStoring(myParam);

	myParam.densitySize.sizeByDensity(myParam.pParticles, myParam.rParticles, myVar.isDensitySizeEnabled, myVar.isForceSizeEnabled,
		myVar.particleSizeMultiplier);

	myParam.particleDeletion.deleteSelected(myParam.pParticles, myParam.rParticles);

	myParam.particleDeletion.deleteStrays(myParam.pParticles, myParam.rParticles, myVar.isCollisionsEnabled, myVar.isSPHEnabled);

	myParam.brush.particlesAttractor(myVar, myParam);

	myParam.brush.particlesSpinner(myVar, myParam);

	myParam.brush.particlesGrabber(myParam);

	myParam.brush.eraseBrush(myParam);

	if (grid != nullptr) {
		delete grid;
	}
}


static void drawScene(Texture2D& particleBlurTex, RenderTexture2D& myUITexture) {

	for (int i = 0; i < myParam.pParticles.size(); ++i) {

		ParticlePhysics& pParticle = myParam.pParticles[i];
		ParticleRendering& rParticle = myParam.rParticles[i];


		// Texture size is set to 16 because that is the particle texture half size in pixels
		DrawTextureEx(particleBlurTex, { static_cast<float>(pParticle.pos.x - rParticle.size * myVar.particleTextureHalfSize),
			static_cast<float>(pParticle.pos.y - rParticle.size * myVar.particleTextureHalfSize) }, 0, rParticle.size, rParticle.color);


		if (!myVar.isDensitySizeEnabled) {

			if (rParticle.canBeResized) {
				rParticle.size = rParticle.previousSize * myVar.particleSizeMultiplier;
			}
			else {
				rParticle.size = rParticle.previousSize;
			}
		}
	}


	myParam.colorVisuals.particlesColorVisuals(myParam.pParticles, myParam.rParticles, myVar.particleSizeMultiplier, myVar.particleTextureHalfSize);

	myParam.trails.drawTrail(myParam.rParticles, particleBlurTex);

	EndTextureMode();
	//EVERYTHING INTENDED TO APPEAR WHILE RECORDING ABOVE


	//END OF PARTICLES RENDER PASS
	//-------------------------------------------------//
	//BEGINNNG OF UI RENDER PASS


	//EVERYTHING NOT INTENDED TO APPEAR WHILE RECORDING BELOW
	BeginTextureMode(myUITexture);

	ClearBackground({ 0,0,0,0 });

	BeginMode2D(myParam.myCamera.camera);

	myVar.mouseWorldPos = GetScreenToWorld2D(GetMousePosition(), myParam.myCamera.camera);
	myParam.brush.drawBrush(myVar.mouseWorldPos);
	DrawRectangleLinesEx({ 0,0, static_cast<float>(myVar.domainSize.x), static_cast<float>(myVar.domainSize.y) }, 3, GRAY);

	// Z-Curves debug toggle
	if (myParam.pParticles.size() > 1 && myVar.drawZCurves) {
		for (size_t i = 0; i < myParam.pParticles.size() - 1; i++) {
			DrawLineV(myParam.pParticles[i].pos, myParam.pParticles[i + 1].pos, WHITE);

			DrawText(TextFormat("%i", i), static_cast<int>(myParam.pParticles[i].pos.x), static_cast<int>(myParam.pParticles[i].pos.y) - 10, 10, { 128,128,128,128 });
		}
	}

	EndMode2D();

	// EVERYTHING NON-STATIC RELATIVE TO CAMERA ABOVE

	// EVERYTHING STATIC RELATIVE TO CAMERA BELOW

	myUI.uiLogic(myParam, myVar, sph);

	myParam.subdivision.subdivideParticles(myVar, myParam);

	EndTextureMode();
}


static void enableMultiThreading() {
	if (myVar.isMultiThreadingEnabled) {
		omp_set_num_threads(myVar.threadsAmount);
	}
	else {
		omp_set_num_threads(1);
	}
}

void fullscreenToggle(int& lastScreenWidth, int& lastScreenHeight, 
	bool& wasFullscreen, bool& lastScreenState, 
	RenderTexture2D& myParticlesTexture, RenderTexture2D& myUITexture) {
	if (IsKeyPressed(KEY_TAB)) {
		myVar.fullscreenState = !myVar.fullscreenState;
	}

	if (myVar.fullscreenState != lastScreenState)
	{
		int monitor = GetCurrentMonitor();

		if (!IsWindowFullscreen())
			SetWindowSize(GetMonitorWidth(monitor), GetMonitorHeight(monitor));
		else
			SetWindowSize(myVar.screenWidth, myVar.screenHeight);

		ToggleFullscreen();
		wasFullscreen = IsWindowFullscreen();

		UnloadRenderTexture(myParticlesTexture);
		UnloadRenderTexture(myUITexture);

		lastScreenWidth = GetScreenWidth();
		lastScreenHeight = GetScreenHeight();
		lastScreenState = myVar.fullscreenState;

		myParticlesTexture = LoadRenderTexture(lastScreenWidth, lastScreenHeight);
		myUITexture = LoadRenderTexture(lastScreenWidth, lastScreenHeight);
	}

	int currentScreenWidth = GetScreenWidth();
	int currentScreenHeight = GetScreenHeight();

	if (currentScreenWidth != lastScreenWidth || currentScreenHeight != lastScreenHeight)
	{
		UnloadRenderTexture(myParticlesTexture);
		UnloadRenderTexture(myUITexture);

		myParticlesTexture = LoadRenderTexture(currentScreenWidth, currentScreenHeight);
		myUITexture = LoadRenderTexture(currentScreenWidth, currentScreenHeight);

		lastScreenWidth = currentScreenWidth;
		lastScreenHeight = currentScreenHeight;
	}
}

int main() {

	SetConfigFlags(FLAG_MSAA_4X_HINT);

	SetConfigFlags(FLAG_WINDOW_RESIZABLE);

	InitWindow(myVar.screenWidth, myVar.screenHeight, "Galaxy Engine");

	Texture2D particleBlurTex = LoadTexture("Textures/ParticleBlur.png");

	Shader myBloom = LoadShader(nullptr, "Shaders/bloom.fs");

	RenderTexture2D myParticlesTexture = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
	RenderTexture2D myUITexture = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());

	SetTargetFPS(myVar.targetFPS);

	int lastScreenWidth = GetScreenWidth();
	int lastScreenHeight = GetScreenHeight();
	bool wasFullscreen = IsWindowFullscreen();

	bool lastScreenState = false;

	while (!WindowShouldClose()) {

		fullscreenToggle(lastScreenWidth, lastScreenHeight, wasFullscreen, lastScreenState, myParticlesTexture, myUITexture);

		

		BeginTextureMode(myParticlesTexture);

		ClearBackground(BLACK);

		BeginBlendMode(myParam.colorVisuals.blendMode);

		BeginMode2D(myParam.myCamera.cameraLogic());

		updateScene();

		drawScene(particleBlurTex, myUITexture);

		EndMode2D();

		EndBlendMode();

		//------------------------ RENDER TEXTURES BELOW ------------------------//

		if (myVar.isGlowEnabled) {
			BeginShaderMode(myBloom);
		}

		//ClearBackground(BLACK);

		//BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);

		DrawTextureRec(
			myParticlesTexture.texture,
			Rectangle{ 0, 0, static_cast<float>(GetScreenWidth()), -static_cast<float>(GetScreenHeight()) },
			Vector2{ 0, 0 },
			WHITE
		);

		if (myVar.isGlowEnabled) {
			EndShaderMode();
		}

		DrawTextureRec(
			myUITexture.texture,
			Rectangle{ 0, 0, static_cast<float>(GetScreenWidth()), -static_cast<float>(GetScreenHeight()) },
			Vector2{ 0, 0 },
			WHITE
		);

		EndBlendMode();

		myVar.isRecording = myParam.screenCapture.screenGrab(myParticlesTexture, myVar);

		if (myVar.isRecording) {
			DrawRectangleLinesEx({ 0,0, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight()) }, 3, RED);
		}

		EndDrawing();


		enableMultiThreading();
	}

	UnloadShader(myBloom);
	UnloadTexture(particleBlurTex);
	UnloadRenderTexture(myParticlesTexture);
	UnloadRenderTexture(myUITexture);

	CloseWindow();



	return 0;
}