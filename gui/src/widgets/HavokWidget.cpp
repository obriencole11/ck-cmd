#include <src/widgets/HavokWidget.h>

constexpr int FPS_LIMIT = 60.0f;
constexpr int MS_PER_FRAME = (int)((1.0f / FPS_LIMIT) * 1000.0f);

#include <Graphics/Common/hkGraphics.h>
#include <Graphics/Common/Camera/hkgCamera.h>
#include <Graphics/Common/Texture/SkyBox/hkgSkyBox.h>
#include <Graphics/Common/Window/hkgWindow.h>
#include <Graphics/Common/DisplayWorld/hkgDisplayWorld.h>

#include <Graphics/Common/DisplayObject/hkgDisplayObject.h>
#include <Graphics/Common/Geometry/hkgGeometry.h>

#include <Graphics/Bridge/System/hkgSystem.h>
#include <Graphics/Bridge/DisplayHandler/hkgDisplayHandler.h>
#include <Graphics/Bridge/SceneData/hkgSceneDataConverter.h>
#include <Graphics/Common/Light/hkgLightManager.h>
#include <Graphics/Common/Material/hkgMaterial.h>
#include <Graphics/Common/Geometry/hkgMaterialFaceSet.h>
#include <Graphics/Common/Geometry/VertexSet/hkgVertexSet.h>
#include <Common\Visualize\hkDebugDisplay.h>
#include <Graphics\Common\Shader\hkgShaderLib.h>
#include <Graphics\Common\Shader\hkgShaderContext.h>

#include <Common/Base/Memory/System/hkMemorySystem.h>

#include <Common/SceneData/Scene/hkxScene.h>
#include <Common/Serialize/Util/hkLoader.h>
#include <Common/Serialize/Util/hkRootLevelContainer.h>

// We need to create bodies
#include <Physics/Dynamics/Entity/hkpRigidBody.h>
#include <Physics/Collide/Shape/Convex/Box/hkpBoxShape.h>
#include <Common\GeometryUtilities\Inertia\hkInertiaTensorComputer.h>
#include <Physics\Utilities\Dynamics\Inertia\hkpInertiaTensorComputer.h>

#include <QApplication>

#include <filesystem>
#include <Animation/Animation/Rig/hkaSkeleton.h>
#include <Animation/Animation/hkaAnimationContainer.h>
#include <src/models/ProjectTreeModel.h>

namespace fs = std::filesystem;
using namespace ckcmd::HKX;

HavokWidget::HavokWidget(ckcmd::HKX::ProjectModel*  model, QWidget* parent) : 
	ads::CDockWidget("Preview", parent),
	_model{ model }
{
	QPalette pal = palette();
	pal.setColor(QPalette::Window, Qt::black);
	setAutoFillBackground(true);
	setPalette(pal);

	setFocusPolicy(Qt::StrongFocus);
	setAttribute(Qt::WA_NativeWindow);

	// Setting these attributes to our widget and returning null on paintEngine event
	// tells Qt that we'll handle all drawing and updating the widget ourselves.
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_NoSystemBackground);

	initialize();

	connect(&m_qTimer, &QTimer::timeout, this, &HavokWidget::paint);
	m_qTimer.start(MS_PER_FRAME);
}

void HavokWidget::drawSkeletalTriangleThingy(hkaSkeleton* skeletal, const std::vector<hkMatrix4>& boneAbsTransform)
{
	hkgDisplayContext* ctx = m_window->getContext();
	HKG_CULLFACE_MODE cullFace = ctx->getCullfaceMode();
	HKG_BLEND_MODE blend = ctx->getBlendMode();

	ctx->matchState(ctx->getEnabledState(), HKG_CULLFACE_CCW, HKG_BLEND_MODULATE, ctx->getAlphaSampleMode());
	ctx->setColorMode(HKG_COLOR_GLOBAL);
	ctx->beginGroup(HKG_IMM_TRIANGLE_LIST);
	ctx->setCurrentGlobalShaderEffectCollection(skeletalShader);
	
	hkgMaterial* matl = hkgMaterial::create();
	matl->setDiffuseColor(1, 1, 1,0.5f);
	matl->setSpecularColor(1, 1, 1);
	matl->setShaderCollection(skeletalShader);
	ctx->setCurrentMaterial(matl, nullptr);

	int i = 0;
	for (auto& p : boneAbsTransform)
	{
		hkInt16 parentIdx = skeletal->m_parentIndices[i];
		if (parentIdx >= 0)
		{
			hkVector4 src, dst;
			boneAbsTransform[parentIdx].getRow(3, src);
			p.getRow(3, dst);

			float a[3] = { src(0), src(1), src(2) };
			float b[3] = { dst(0), dst(1), dst(2) };

			float sub[3];
			hkgVec3Sub(sub, b, a);
			float boneLen = hkgVec3Length(sub);
			hkgVec3Normalize(sub);
			float left[3] = { 1,0,0 };

			float axisRot[3];
			hkgVec3Cross(axisRot, left, sub);
			hkgVec3Normalize(axisRot);
			float angle = std::acosf(hkgVec3Dot(left, sub));
			float quat[4];
			hkgQuatFromAngleAxis(quat, angle, axisRot);
			
			float num5 = boneLen / 14.0f;
			float x = boneLen / 5.0f;
			float pyrPoints[6 * 3];
			hkgVec3Set(&pyrPoints[0], boneLen, 0, 0);
			hkgVec3Set(&pyrPoints[3], x, num5, num5);
			hkgVec3Set(&pyrPoints[6], x, -num5, num5);
			hkgVec3Set(&pyrPoints[9], x, num5, -num5);
			hkgVec3Set(&pyrPoints[12], x, -num5, -num5);
			hkgVec3Set(&pyrPoints[15], 0, 0, 0);

			for (int j = 0; j <= 15; j += 3)
			{
				hkgQuatRotateVec3(&pyrPoints[j], quat, &pyrPoints[j]);
				hkgVec3Add(&pyrPoints[j], a);
			}

			constexpr int pyrTris[24] = {
				0,3,6,
				0,9,3,
				0,12,9,
				0,6,12,
				15,6,3,
				15,3,9,
				15,9,12,
				15,12,6
			};

			for (int k = 0; k <= 21; k+=3)
			{
				float leftComp[3];
				float right[3];
				float out[3];
				hkgVec3Sub(leftComp, &pyrPoints[pyrTris[k+1]], &pyrPoints[pyrTris[k]]);
				hkgVec3Sub(right, &pyrPoints[pyrTris[k+2]], &pyrPoints[pyrTris[k]]);
				hkgVec3Cross(out, leftComp, right);
				hkgVec3Normalize(out);

				ctx->setCurrentColorPacked(0x80858585);
				ctx->setCurrentNormal(out);

				ctx->setCurrentPosition(&pyrPoints[pyrTris[k]]);
				ctx->setCurrentPosition(&pyrPoints[pyrTris[k+1]]);
				ctx->setCurrentPosition(&pyrPoints[pyrTris[k+2]]);
			}
		}
		++i;
	}

	ctx->endGroup();
	
	//ctx->matchState(ctx->getEnabledState(), cullFace, blend, ctx->getAlphaSampleMode());
}

void HavokWidget::setupScene()
{
	m_loader = new hkLoader();
	// Get and convert the scene - this is just a ground grid plane and a camera
	//fs::path scene_path = fs::path(qApp->applicationDirPath().toUtf8().constData()) / "Capt_Hi.hkx";
	//hkRootLevelContainer* container = m_loader->load(scene_path.string().c_str());
	//HK_ASSERT2(0x27343437, container != HK_NULL , "Could not load asset");
	//auto animContainer = reinterpret_cast<hkaAnimationContainer*>(container->findObjectByType(hkaAnimationContainerClass.getName()));
	//skeleton = animContainer->findSkeletonByName("Bip01 Pelvis");
	//updateSkeleton();

	hkgDisplayContext* ctx = m_window->getContext();
	hkgShader* vsShader = hkgShader::createVertexShader(ctx);
	hkgShader* psShader = hkgShader::createPixelShader(ctx);

	fs::path vsp_path = fs::path(qApp->applicationDirPath().toUtf8().constData()) / "bone_vs.hlsl";
	fs::path psp_path = fs::path(qApp->applicationDirPath().toUtf8().constData()) / "bone_ps.hlsl";
	vsShader->realizeCompileFromFile(vsp_path.string().c_str(), "main", HKG_SHADER_RENDER_NOSTYLE);
	psShader->realizeCompileFromFile(psp_path.string().c_str(), "main", HKG_SHADER_RENDER_NOSTYLE);

	skeletalShader = hkgShaderEffectCollection::create();
	skeletalShader->addShaderEffect(vsShader, psShader);

	return;

	/*
	hkxScene* scene = new hkxScene();

	//HK_ASSERT2(0x27343635, scene, "No scene loaded");
	//removeLights(m_env); // assume we have some in the file
	//m_sceneConverter->convert(scene);
	*/
}

void HavokWidget::createGridMesh()
{
	auto* ctx = m_window->getContext();
	hkRefPtr<hkgTexture> text = hkgTexture::create(ctx);
	if (!text->loadBuiltIn(HKG_TEXTURE_CHECKER)) return;
	//hkIstream iStream{ "checker_bg.png" };
	//text->loadFromPNG(iStream);
	text->setAutoMipMaps(true);
	if (!text->realize(false))
	{
		return;
	}

	hkgDisplayObject* dispObj = hkgDisplayObject::create();
	hkRefPtr<hkgGeometry> geom = hkgGeometry::create();
	geom->makeSingleFaceSet(ctx);
	float gridSize = 500.0f;
	float numGridCells = 5.0f;
	float subGridSpace = numGridCells / gridSize;

	float corner[3], sideA[3], sideB[3];
	hkgVec3Set(corner,-gridSize / 2.0f, -gridSize / 2.0f, 0.0f);
	hkgVec3Set(sideA, .0f, gridSize, .0f);
	hkgVec3Set(sideB, gridSize, .0f, .0f);
	int num5 = 2;

	hkgMaterialFaceSet* matlFaceSet = geom->getMaterialFaceSet(0);
	hkgFaceSet* faceSet = matlFaceSet->getFaceSet(0);

	faceSet->makeGrid(corner, sideA, sideB, numGridCells, numGridCells, 1, false, false);
	hkgVertexSet* vertexSet = faceSet->getVertexSet();
	int numVerts = vertexSet->getNumVerts();
	vertexSet->lock(HKG_LOCK_DEFAULT);

	// Map UV
	for (int i = 0; i < numVerts; ++i)
	{
		float* vec = (float*)vertexSet->getVertexComponentData(HKG_VERTEX_COMPONENT_POS, i);
		float uv[2] = { vec[0] * subGridSpace, vec[1] * subGridSpace };
		vertexSet->setVertexComponentData(HKG_VERTEX_COMPONENT_TEX0, i, uv);
	}

	vertexSet->unlock();

	hkMatrix4 m_matrix = hkMatrix4::getIdentity();
	hkReal mtx[16];
	m_matrix.get4x4ColumnMajor(mtx);
	dispObj->setTransform(mtx);

	hkgMaterial* matl = hkgMaterial::create();
	matl->setDiffuseColor(1, 1, 1);
	matl->setSpecularColor(0, 0, 0);
	matl->addTexture(text);
	matlFaceSet->setMaterial(matl);

	dispObj->addGeometry(geom);
	dispObj->setStatusFlags(HKG_DISPLAY_OBJECT_SHADOWRECEIVER | HKG_DISPLAY_OBJECT_SHADOWCASTER | dispObj->getStatusFlags());
	m_displayWorld->addDisplayObject(dispObj);
}

void HavokWidget::setupFixedShadowFrustum(const hkgLight& light, const hkgAabb& areaOfInterest, float extraNear, float extraFar, int numSplits, int preferedUpAxis)
{
	hkgCamera* lightCam = hkgCamera::createFixedShadowFrustumCamera(light, areaOfInterest, true, extraNear, extraFar, preferedUpAxis);

	HKG_SHADOWMAP_SUPPORT shadowSupport = m_window->getShadowMapSupport();
	if ((numSplits > 0) && (shadowSupport == HKG_SHADOWMAP_VSM))
	{
		m_window->setShadowMapSplits(numSplits); // > 0 and you are requesting PSVSM (if the platforms supports VSM that is)
		m_window->setShadowMapMode(HKG_SHADOWMAP_MODE_PSVSM, lightCam);
	}
	else
	{
		if ((numSplits > 0) && (shadowSupport != HKG_SHADOWMAP_NOSUPPORT))
		{
			static int once = 0;
			if (once == 0)
			{
				HK_WARN_ALWAYS(0x0, "The demo is requesting PSVSM shadows, but VSM is not supported, so just reverting to normal single map, fixed projection.");
				once = 1;
			}
		}

		m_window->setShadowMapMode(HKG_SHADOWMAP_MODE_FIXED, lightCam);
	}

	lightCam->removeReference();
}

void HavokWidget::setupLights()
{
	// make some default lights
	hkgLightManager* lm = m_displayWorld->getLightManager();

	if (!lm)
	{
		lm = hkgLightManager::create();
		m_displayWorld->setLightManager(lm);
		//lm->release();
		//lm->lock();
	}
	else
	{
		// clear out the lights currently in the world.
		while (lm->getNumLights() > 0)
		{
			hkgLight* l = lm->removeLight(0); // gives back reference
			l->release();
		}
	}
	
	// Background color
	float bg[4] = { 0.53f, 0.55f, 0.61f, 1 };
	m_window->setClearColor(bg);

	lm->lock();
	lm->setSceneAmbient(HKG_VEC3_ZERO);

	float v[4]; v[3] = 255;
	hkgLight* light;


	// the sun (direction downwards)
	{
		light = hkgLight::create();
		light->setType(HKG_LIGHT_DIRECTIONAL);
		v[0] = 256;
		v[1] = 256;
		v[2] = 256;
		v[0] /= 255; v[1] /= 255; v[2] /= 255;
		light->setDiffuse(v);
		light->setSpecular(v);
		v[0] = 0;
		v[1] = -1;
		v[2] = -0.5f;
		light->setDirection(v);
		v[0] = 0;
		v[1] = 1000;
		v[2] = 0;
		light->setPosition(v);
		light->setDesiredEnabledState(true);
		lm->addLight(light);

		// float shadowPlane[] = { 0,1,0,-0.01f };
		// light->addShadowPlane( shadowPlane );
		light->release();
	}

	hkgAabb areaOfInterest;
	areaOfInterest.m_max[0] = 10;
	areaOfInterest.m_max[1] = 10;
	areaOfInterest.m_max[2] = 10;
	areaOfInterest.m_min[0] = -10;
	areaOfInterest.m_min[1] = -10;
	areaOfInterest.m_min[2] = -10;
	setupFixedShadowFrustum(*light, areaOfInterest);
	
	// if se have shadow maps we only support on light by default (or else it looks dodge)
	//if (!m_enableShadows || (env->m_window->getShadowMapSupport() == HKG_SHADOWMAP_NOSUPPORT))
	//{
	//	// fill 1 - blue
	//	{
	//		light = hkgLight::create();
	//		light->setType(HKG_LIGHT_DIRECTIONAL);
	//		v[0] = 200;
	//		v[1] = 200;
	//		v[2] = 240;
	//		v[0] /= 255; v[1] /= 255; v[2] /= 255;
	//		light->setDiffuse(v);
	//		v[0] = 1;
	//		v[1] = 1;
	//		v[2] = 1;
	//		light->setDirection(v);
	//		v[0] = -1000;
	//		v[1] = -1000;
	//		v[2] = -1000;
	//		light->setPosition(v);
	//		light->setDesiredEnabledState(true);
	//		lm->addLight(light);
	//		light->release();
	//	}

	//	// fill 2 - yellow
	//	{
	//		light = hkgLight::create();
	//		light->setType(HKG_LIGHT_DIRECTIONAL);
	//		v[0] = 240;
	//		v[1] = 240;
	//		v[2] = 200;
	//		v[0] /= 255; v[1] /= 255; v[2] /= 255;
	//		light->setDiffuse(v);
	//		v[0] = -1;
	//		v[1] = 1;
	//		v[2] = -1;
	//		light->setDirection(v);
	//		v[0] = 1000;
	//		v[1] = -1000;
	//		v[2] = 1000;
	//		light->setPosition(v);
	//		light->setDesiredEnabledState(true);
	//		lm->addLight(light);
	//		light->release();
	//	}
	//}
	
	/*
	lm->addDefaultLights(200, HKG_VEC3_Z, HKG_VEC3_X, 1);

	//hkgAabb areaOfInterest;
	areaOfInterest.m_max[0] = 10;
	areaOfInterest.m_max[1] = 10;
	areaOfInterest.m_max[2] = 10;
	areaOfInterest.m_min[0] = -10;
	areaOfInterest.m_min[1] = -10;
	areaOfInterest.m_min[2] = -10;
	light = lm->getLight(0);
	setupFixedShadowFrustum(*light, areaOfInterest);
	*/
	lm->computeActiveSet(HKG_VEC3_ZERO);
	lm->unlock();
}

void HavokWidget::initialize()
{
	hkgSystem::init("d3d9s");
	m_window = hkgWindow::create();
	m_window->setWantDrawHavokLogo(false);
	m_window->setWantDrawMousePointer(false);
	m_window->setShadowMapSize(0); // 0 == use default platform size
	HKG_WINDOW_CREATE_FLAG windowFlags = HKG_WINDOW_WINDOWED | HKG_WINDOW_MSAA;

	m_window->initialize(windowFlags,
		HKG_WINDOW_BUF_COLOR | HKG_WINDOW_BUF_DEPTH32, size().width(), size().height(),
		"Preview", (void*)QWidget::winId());
	m_window->getContext()->lock();

	// don't allow viewport resizing
	m_window->setWantViewportBorders(false);
	m_window->setWantViewportResizeByMouse(false);

	auto* viewport = m_window->getViewport(0);
	viewport->setNavigationMode(HKG_CAMERA_NAV_TRACKBALL);
	viewport->setMouseConvention(HKG_MC_MAYA);

	viewport->setDesiredState(63);
	viewport->setDesiredCullFaceMode(HKG_CULLFACE_CCW);

	for (int i = 0; i < 2; ++i)
	{
		m_window->clearBuffers();
		m_window->swapBuffers();
	}

	m_displayWorld = hkgDisplayWorld::create();

	m_skeletalWorld = hkgDisplayWorld::create();
	m_skeletalWorld->setLightManager(hkgLightManager::create());
	m_skeletalWorld->getLightManager()->addDefaultLights(100, HKG_VEC3_Z, HKG_VEC3_Y, 1);

	m_sceneConverter = new hkgSceneDataConverter(m_displayWorld, m_window->getContext());
	m_displayHandler = new hkgDisplayHandler(m_displayWorld, m_window->getContext(), m_window);
	m_displayHandler->setShaderLib(m_sceneConverter->m_shaderLibrary);

	setupLights();
	setupScene();

	setupCamera();

	m_window->getCurrentViewport()->setFlyModeUp(HKG_VEC3_Z);

	createGridMesh();
	updateSkeleton();
	m_window->getContext()->unlock();
}

void HavokWidget::setupCamera()
{
	constexpr float CAM_FROM[3] = { 50,50,50 };
	constexpr float CAM_TO[3] = { 0,0,0 };
	constexpr float CAM_FOV = 0.7853982;

	float front[3] = { 0,1,0 };
	hkgCamera* camera = m_window->getCurrentViewport()->getCamera();
	camera->setFrom(CAM_FROM);
	camera->setTo(CAM_TO);
	camera->setUp(HKG_VEC3_Z);
	camera->setFOV(CAM_FOV * 180.0f / 3.1415927f);
	camera->setNear(0.1f);
	camera->setFar(5000);
	camera->setAspect(CAM_FOV);
	camera->setProjectionMode(HKG_CAMERA_PERSPECTIVE);

	camera->computeModelView(false);
	camera->computeProjection();
}

void HavokWidget::resizeEvent(QResizeEvent* event)
{
	m_window->getContext()->lock();
	m_window->updateSize(event->size().width(), event->size().height());
	m_window->getContext()->unlock();
}

void HavokWidget::mouseMoveEvent(QMouseEvent* event)
{
	m_window->processMouseMove(event->pos().x(), m_window->getHeight() - event->pos().y() - 1, false);
}

void HavokWidget::mousePressEvent(QMouseEvent* event)
{
	HKG_MOUSE_BUTTON btn;
	switch (event->button())
	{
	case Qt::MouseButton::LeftButton:
		btn = HKG_MOUSE_LEFT_BUTTON;
		break;
	case Qt::MouseButton::RightButton:
		btn = HKG_MOUSE_RIGHT_BUTTON;
		break;
	case Qt::MouseButton::MiddleButton:
	case Qt::MouseButton::XButton1:
	case Qt::MouseButton::XButton2:
		btn = HKG_MOUSE_MIDDLE_BUTTON;
		break;
	default:
		return;
	}
	m_window->processMouseButton(btn, true, event->pos().x(), m_window->getHeight() - event->pos().y() - 1, false);
}

void HavokWidget::mouseReleaseEvent(QMouseEvent* event)
{
	HKG_MOUSE_BUTTON btn;
	switch (event->button())
	{
	case Qt::MouseButton::LeftButton:
		btn = HKG_MOUSE_LEFT_BUTTON;
		break;
	case Qt::MouseButton::RightButton:
		btn = HKG_MOUSE_RIGHT_BUTTON;
		break;
	case Qt::MouseButton::MiddleButton:
	case Qt::MouseButton::XButton1:
	case Qt::MouseButton::XButton2:
		btn = HKG_MOUSE_MIDDLE_BUTTON;
		break;
	default:
		return;
	}
	m_window->processMouseButton(btn, false, event->pos().x(), m_window->getHeight() - event->pos().y() - 1, false);
}

void HavokWidget::wheelEvent(QWheelEvent* event)
{
	m_window->processMouseWheel(event->delta()/2, event->pos().x(), m_window->getHeight() - event->pos().y() - 1, false);
}

void HavokWidget::showAxis()
{
	uint color = 0xFF0000;
	constexpr uint alphaMask = (0xFF << 24);
	static const float array[102] =
	{
		0.0f,
		0.0f,
		0.0f,
		1.0f,
		0.0f,
		0.0f,
		1.0f,
		0.0f,
		0.0f,
		0.85f,
		0.1f,
		0.0f,
		1.0f,
		0.0f,
		0.0f,
		0.85f,
		-0.1f,
		0.0f,
		1.1f,
		0.1f,
		0.0f,
		1.2f,
		-0.1f,
		0.0f,
		1.2f,
		0.1f,
		0.0f,
		1.1f,
		-0.1f,
		0.0f,
		0.0f,
		0.0f,
		0.0f,
		0.0f,
		1.0f,
		0.0f,
		0.0f,
		1.0f,
		0.0f,
		0.1f,
		0.85f,
		0.0f,
		0.0f,
		1.0f,
		0.0f,
		-0.1f,
		0.85f,
		0.0f,
		0.1f,
		1.1f,
		0.0f,
		0.0f,
		1.15f,
		0.0f,
		0.1f,
		1.2f,
		0.0f,
		0.0f,
		1.15f,
		0.0f,
		0.0f,
		1.15f,
		0.0f,
		-0.1f,
		1.15f,
		0.0f,
		0.0f,
		0.0f,
		0.0f,
		0.0f,
		0.0f,
		1.0f,
		0.0f,
		0.0f,
		1.0f,
		0.1f,
		0.0f,
		0.85f,
		0.0f,
		0.0f,
		1.0f,
		-0.1f,
		0.0f,
		0.85f,
		0.1f,
		0.0f,
		1.1f,
		0.1f,
		0.0f,
		1.2f,
		0.1f,
		0.0f,
		1.2f,
		-0.1f,
		0.0f,
		1.1f,
		-0.1f,
		0.0f,
		1.1f,
		-0.1f,
		0.0f,
		1.2f
	};

	if (!m_showWorldAxis)
		return;

	auto* ctx = m_window->getContext();
	HKG_ENABLED_STATE enabledState = ctx->getEnabledState();

	ctx->matchState(HKG_ENABLED_NONE, ctx->getCullfaceMode(), ctx->getBlendMode(), ctx->getAlphaSampleMode());
	float m[16];
	float result[3];
	float temp[3];

	hkgMat4Identity(m);
	hkgVec3Zero(result);
	hkgVec3Zero(temp);

	hkgViewport* viewport = m_window->getViewport(0);
	hkgCamera* camera = viewport->getCamera();
	camera->unProject(40, 40, 0.2f, viewport->getWidth(), viewport->getHeight(), result);
	camera->getFrom(temp);
	hkgVec3Sub(temp, result);

	float distanceFromCam = hkgVec3Length(temp);
	float scale = camera->computeIconVerticalDrawSize(distanceFromCam, 32, viewport->getHeight());
	ctx->pushMatrix();
	m[12] = result[0];
	m[13] = result[1];
	m[14] = result[2];

	ctx->multMatrix(m);

	ctx->beginGroup(HKG_IMM_LINES);
	int index = 0;
	float currentPosition[3];
	hkgVec3Zero(currentPosition);


	float pos[3];
	for (int i = 0; i < 5; i++) 
	{
		ctx->setCurrentColorPacked(color | alphaMask);
		hkgVec3Copy(pos, &array[index]); 
		index += 3;
		hkgVec3Scale(pos, -scale);
		ctx->setCurrentPosition(pos);

		ctx->setCurrentColorPacked(color | alphaMask);
		hkgVec3Copy(pos, &array[index]);
		index += 3;
		hkgVec3Scale(pos, -scale);
		ctx->setCurrentPosition(pos);
	}

	color >>= 8;
	for (int i = 0; i < 6; i++)
	{
		ctx->setCurrentColorPacked(color | alphaMask);
		hkgVec3Copy(pos, &array[index]);
		index += 3;
		hkgVec3Scale(pos, -scale);
		ctx->setCurrentPosition(pos);

		ctx->setCurrentColorPacked(color | alphaMask);
		hkgVec3Copy(pos, &array[index]);
		index += 3;
		hkgVec3Scale(pos, -scale);
		ctx->setCurrentPosition(pos);
	}

	color >>= 8;
	for (int i = 0; i < 6; i++)
	{
		ctx->setCurrentColorPacked(color | alphaMask);
		hkgVec3Copy(pos, &array[index]);
		index += 3;
		hkgVec3Scale(pos, scale);
		ctx->setCurrentPosition(pos);

		ctx->setCurrentColorPacked(color | alphaMask);
		hkgVec3Copy(pos, &array[index]);
		index += 3;
		hkgVec3Scale(pos, scale);
		ctx->setCurrentPosition(pos);
	}
	ctx->endGroup();

	ctx->popMatrix();
	ctx->matchState(enabledState, ctx->getCullfaceMode(), ctx->getBlendMode(),ctx->getAlphaSampleMode());
}

void HavokWidget::drawGrid()
{
	if (!m_showGrid)
		return;

	hkgDisplayContext* ctx = m_window->getContext();
	float num = 100.0f;

	auto enabledState = ctx->getEnabledState();
	auto cullfaceMode = ctx->getCullfaceMode();
	auto blendMode = ctx->getBlendMode();

	ctx->matchState(HKG_ENABLED_ALPHABLEND|HKG_ENABLED_ZREAD, HKG_CULLFACE_CCW, HKG_BLEND_ADD, ctx->getAlphaSampleMode());
	ctx->beginGroup(HKG_IMM_LINES);
	
	float p[3];
	float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

	for (int i = 0; i <= 100; ++i)
	{
		float num2 = -0.5f * num + i;
		color[3] = !(i % 10) ? 1.0f : 0.15f;

		hkgVec3Set(p, num2, 0, 0);
		ctx->setCurrentColor4(color);
		ctx->setCurrentPosition(p);

		hkgVec3Set(p, num2, 0.5f * num, 0);
		ctx->setCurrentColor4(color);
		ctx->setCurrentPosition(p);

		hkgVec3Set(p, num2, 0, 0);
		ctx->setCurrentColor4(color);
		ctx->setCurrentPosition(p);

		hkgVec3Set(p, num2, -0.5f * num, 0);
		ctx->setCurrentColor4(color);
		ctx->setCurrentPosition(p); 
	}
	
	for (int j = 0; j <= 100; ++j)
	{
		float num3 = -0.5f * num + (float)j;
		color[3] = (j % 10 == 0) ? 1.0f : 0.15f;

		hkgVec3Set(p, 0, num3, 0);
		ctx->setCurrentColor4(color);
		ctx->setCurrentPosition(p);

		hkgVec3Set(p, 0.5f * num, num3, 0);
		ctx->setCurrentColor4(color);
		ctx->setCurrentPosition(p);


		hkgVec3Set(p, 0, num3, 0);
		ctx->setCurrentColor4(color);
		ctx->setCurrentPosition(p);

		hkgVec3Set(p, -0.5f * num, num3, 0);
		ctx->setCurrentColor4(color);
		ctx->setCurrentPosition(p);
	}
	ctx->endGroup();
	ctx->matchState(enabledState, cullfaceMode, blendMode, ctx->getAlphaSampleMode());
}

void HavokWidget::relinkSkeleton()
{
	for (int i = 0; i < boneAbsTransforms.size(); ++i)
	{
		hkInt16 num = skeleton->m_parentIndices[i];
		if (num >= 0)
		{
			hkVector4 src{};
			hkVector4 dst{};

			boneAbsTransforms[num].getRow(3, src);
			boneAbsTransforms[i].getRow(3, dst);

			float a[3] = { src(0), src(1), src(2) };
			float b[3] = { dst(0), dst(1), dst(2) };
			hkgDisplayObject* dispObj = hkgDisplayObject::create();
			dispObj->setName("skeleton");
			hkgGeometry* geom = createBoneVertices(b, a);
			dispObj->setStatusFlags(
				HKG_DISPLAY_OBJECT_SHADOWCASTER | HKG_DISPLAY_OBJECT_SHADOWRECEIVER | dispObj->getStatusFlags());
			dispObj->addGeometry(geom);
			m_skeletalWorld->addDisplayObject(dispObj);
		}
	}
}

hkRefPtr<hkgGeometry> HavokWidget::createBoneVertices(const float vTarget[3], const float vOrigin[3])
{
	hkgDisplayContext* context = m_window->getContext();

	hkgGeometry* geom = hkgGeometry::create();
	createDoublePyramid(vTarget, vOrigin, geom, context);
	hkgMaterial* matl = hkgMaterial::create();
	matl->setDiffuseColor(1, 1, 1, 1);
	matl->setSpecularColor(1, 1, 1);
	if (matl != HK_NULL && skeletalShader != HK_NULL)
	{
		matl->setShaderCollection(skeletalShader);
	}
	for (int i = 0; i < geom->getNumMaterialFaceSets(); ++i)
	{
		geom->getMaterialFaceSet(i)->setMaterial(matl);
	}
	geom->computeAABB();

	return geom;
}

void HavokWidget::createDoublePyramid(const float vTarget[3], const float vOrigin[3], hkgGeometry* geom, hkgDisplayContext* context)
{

	/*
	constexpr int VERT_LEN = 24;

	float sub[3];
	hkgVec3Sub(sub, vA, vB);
	float boneLen = hkgVec3Length(sub);

	hkgFaceSetPrimitive* faceSetPrim = new hkgFaceSetPrimitive(HKG_TRI_LIST);
	faceSetPrim->setLength(VERT_LEN, true, HKG_INDICES_UINT16);
	faceSetPrim->setVertexBufferStartIndex(0);
	hkUint16* indices = faceSetPrim->getIndices16();
	for (hkUint16 i = 0; i < faceSetPrim->getLength(); i++)
	{
		*indices = i;
	}
	faceSet->addPrimitive(faceSetPrim);

	HKG_VERTEX_FORMAT vertFmt = 0;
	vertFmt |= HKG_VERTEX_FORMAT_POS;
	vertFmt |= HKG_VERTEX_FORMAT_COLOR;
	vertFmt |= HKG_VERTEX_FORMAT_NORMAL;

	hkRefPtr<hkgVertexSet> vertexSet = hkgVertexSet::create(context);
	vertexSet->setNumVerts(VERT_LEN, vertFmt);
	vertexSet->lock(HKG_LOCK_DEFAULT);
	
	const uint32_t whiteColor = hkColor::WHITE; 0xFFFFFFFF;
	for (int i = 0; i < VERT_LEN; i++)
	{

		vertexSet->setVertexComponentData(HKG_VERTEX_COMPONENT_COLOR, i, &whiteColor);
	}
for (int k = 0; k < std::size(pyrTris); k++)
{
	vertexSet->setVertexComponentData(HKG_VERTEX_COMPONENT_POS, k, &pyrPoints[pyrTris[k]]);
	if ((k + 1) % 3 == 0)
	{
		float left[3];
		float right[3];
		float out[3];
		hkgVec3Sub(left, &pyrPoints[pyrTris[k - 1]], &pyrPoints[pyrTris[k - 2]]);
		hkgVec3Sub(right, &pyrPoints[pyrTris[k]], &pyrPoints[pyrTris[k - 2]]);
		hkgVec3Cross(out, left, right);
		hkgVec3Normalize(out);
		vertexSet->setVertexComponentData(HKG_VERTEX_COMPONENT_NORMAL, k - 2, out);
		vertexSet->setVertexComponentData(HKG_VERTEX_COMPONENT_NORMAL, k - 1, out);
		vertexSet->setVertexComponentData(HKG_VERTEX_COMPONENT_NORMAL, k, out);
	}
}

vertexSet->unlock();
faceSet->setVertexSet(vertexSet);*/
	  
	float sub[3];
	hkgVec3Sub(sub, vTarget, vOrigin);
	float boneLen = hkgVec3Length(sub);
	hkgVec3Normalize(sub);
	float left[3] = { 1,0,0 };

	float axisRot[3];
	hkgVec3Cross(axisRot, left, sub);
	hkgVec3Normalize(axisRot);
	float angle = std::acosf(hkgVec3Dot(left, sub));
	float quat[4];
	hkgQuatFromAngleAxis(quat, angle, axisRot);

	float num5 = boneLen / 14.0f;
	float x = boneLen / 5.0f;
	float pyrPoints[6 * 3];
	hkgVec3Set(&pyrPoints[0], boneLen, 0, 0);
	hkgVec3Set(&pyrPoints[3], x, num5, num5);
	hkgVec3Set(&pyrPoints[6], x, -num5, num5);
	hkgVec3Set(&pyrPoints[9], x, num5, -num5);
	hkgVec3Set(&pyrPoints[12], x, -num5, -num5);
	hkgVec3Set(&pyrPoints[15], 0, 0, 0);

	
	for (int j = 0; j <= 15; j += 3)
	{
		hkgQuatRotateVec3(&pyrPoints[j], quat, &pyrPoints[j]);
		hkgVec3Add(&pyrPoints[j], vOrigin);
	}

	float mid1[3] = { x,0,0 };
	hkgQuatRotateVec3(mid1, quat, mid1);
	hkgVec3Add(mid1, vOrigin);

	geom->makeSingleFaceSet(context);
	geom->makeSingleFaceSet(context);
	auto* matlFaceSet = geom->getMaterialFaceSet(0);
	auto* faceSetDown = matlFaceSet->getFaceSet(0);
	
	matlFaceSet = geom->getMaterialFaceSet(1);
	auto* faceSetUp = matlFaceSet->getFaceSet(0);

	faceSetDown->makeCone(&pyrPoints[15], mid1, 0, num5*1.5f,5,5);
	faceSetUp->makeCone(mid1, pyrPoints, num5*1.5f, 0,5,5);
}

void HavokWidget::updateSkeleton()
{
	if (skeleton == nullptr)
		return;

	int dispObjCnt = m_skeletalWorld->getNumDisplayObjects();
	for (int i = 0; i < dispObjCnt; ++i)
	{
		auto* dispObj = m_skeletalWorld->removeDisplayObject(0);
		dispObj->release();
	}

	boneAbsTransforms.clear();
	boneModelSpaces.clear();


	int i = 0;
	hkArray<hkQsTransform>& pose = skeleton->m_referencePose;
	for (auto& p : pose)
	{
		hkReal temp[16];
		p.get4x4ColumnMajor(temp);
		hkMatrix4 m_pose;
		m_pose.set4x4RowMajor(temp);

		hkInt16 num = skeleton->m_parentIndices[i];
		hkMatrix4 m_item{};

		if (num >= 0)
		{
			m_item.setMul(m_pose, boneAbsTransforms[num]);
			m_pose.mul(boneModelSpaces[num]);
		}
		else
		{
			m_item = m_pose;
		}
		boneAbsTransforms.push_back(m_item);
		boneModelSpaces.push_back(m_pose);

		++i;
	}

	relinkSkeleton();
}

void HavokWidget::drawSkeletalNormals()
{
	if (!skeleton) return;

	hkgDisplayContext* ctx = m_window->getContext();

	auto enabledState = ctx->getEnabledState();
	auto cullfaceMode = ctx->getCullfaceMode();
	auto blendMode = ctx->getBlendMode();

	ctx->matchState(HKG_ENABLED_ALPHABLEND | HKG_ENABLED_ZREAD, HKG_CULLFACE_CCW, HKG_BLEND_ADD, ctx->getAlphaSampleMode());
	ctx->beginGroup(HKG_IMM_LINES);
	float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	for (int i = 0; i < boneAbsTransforms.size(); ++i)
	{
		hkInt16 num = skeleton->m_parentIndices[i];
		if (num >= 0)
		{
			hkVector4 src;
			hkVector4 dst;

			boneAbsTransforms[num].getRow(3, src);
			boneAbsTransforms[i].getRow(3, dst);

			float a[3] = { src(0), src(1), src(2) };
			float b[3] = { dst(0), dst(1), dst(2) };


			ctx->setCurrentColor4(color);
			ctx->setCurrentPosition(a);

			ctx->setCurrentColor4(color);
			ctx->setCurrentPosition(b);

			//Normals
			hkVector4 x = boneAbsTransforms[num].getColumn(0);
			hkVector4 y = boneAbsTransforms[num].getColumn(1);
			hkVector4 z = boneAbsTransforms[num].getColumn(2);

			hkArray<hkVector4> compArray{};

			dst.sub4(src);
			float num2 = dst.length3();
			float num3 = (x.length3() + y.length3() + z.length3()).getReal();
			num2 /= num3;

			compArray.pushBack(src);
			compArray.pushBack(src);
			compArray[1].addMul4(num2, x);

			compArray.pushBack(src);
			compArray.pushBack(src);
			compArray[3].addMul4(num2, y);

			compArray.pushBack(src);
			compArray.pushBack(src);
			compArray[5].addMul4(num2, z);

			for (int k = 0; k < 6; ++k)
			{
				if (k < 2) ctx->setCurrentColorPacked(4294901760U);
				else if (k < 4) ctx->setCurrentColorPacked(4278255360U);
				else ctx->setCurrentColorPacked(4278190335U);

				float vec[3];
				compArray[k].store3(vec);

				ctx->setCurrentPosition(vec);
			}
		}
	}
	ctx->matchState(enabledState, cullfaceMode, blendMode, ctx->getAlphaSampleMode());
	ctx->endGroup();

	//drawSkeletalTriangleThingy(skeleton,boneAbsTransforms);
}

void HavokWidget::tickFrame(bool justCamera)
{
	hkgDisplayContext* ctx = m_window->getContext();
	ctx->lock();
	int frameNum = ctx->advanceFrame();
	// Primary world:
	if (m_displayWorld)
	{
		m_displayWorld->advanceToFrame(frameNum, !justCamera, m_window);
		m_window->getViewport(0)->getCamera()->advanceToFrame(frameNum);
	}
	ctx->unlock();
}

void HavokWidget::clearFrameData()
{

}

void HavokWidget::renderFrame()
{
	HKG_TIMER_BEGIN_LIST("_Render", "render");

	hkgWindow* window = m_window;

	if (!window || (window->getWidth() == 0) || (window->getHeight() == 0))
	{
		HKG_TIMER_BEGIN("SwapBuffers", HK_NULL);
		if (window)
		{
			window->getContext()->lock();
			window->swapBuffers();
			window->getContext()->unlock();
		}
		HKG_TIMER_END();

		clearFrameData();
		HKG_TIMER_END_LIST(); // render
		return; // nothing to render too..
	}

	hkgDisplayContext* ctx = window->getContext();
	ctx->lock();

	hkgViewport* masterView = window->getCurrentViewport();

	window->inViewportResize();

	HKG_TIMER_SPLIT_LIST("SetViewport");

	masterView->setAsCurrent(ctx);


	if (masterView->getSkyBox())
	{
		HKG_TIMER_SPLIT_LIST("SkyBox");
		//masterView->getSkyBox()->render(ctx, masterView->getCamera());
	}

	hkgDisplayWorld* dw = m_displayWorld;

	HKG_TIMER_SPLIT_LIST("DisplayWorld");
	if (dw)
	{
		// can't alter the world in the middle of a render pass, so it will lock itself
		dw->render(ctx, true, true); // culled with shadows (if any setup)
	}
	if (m_skeletalWorld)
	{
		m_skeletalWorld->render(ctx, true, true, false);
	}

	HKG_TIMER_SPLIT_LIST("DrawImmediate");
	hkgDisplayHandler* dh = m_displayHandler;
	if (dh)
	{
		dh->drawImmediate();
	}
	
	// Draw Gizmos
	showAxis();
	if (m_skelNeedsUpdate)
	{
		updateSkeleton();
		m_skelNeedsUpdate = false;
	}
	drawSkeletalNormals();

	HKG_TIMER_SPLIT_LIST("PostEffects");

	m_window->applyPostEffects();

	HKG_TIMER_SPLIT_LIST("Final Pass Objects");
	{
		dw->finalRender(ctx, true /* frustum cull */); 
	}

	window->stepInput();

	HKG_TIMER_SPLIT_LIST("SwapBuffers");

	window->swapBuffers();

	//env.m_inRenderLoop = false;

	ctx->unlock();

	HKG_TIMER_END_LIST(); // render
}

void HavokWidget::paint()
{
	if (isVisible())
	{
		m_window->clearBuffers();

		hkMemorySystem::getInstance().advanceFrame();

		renderFrame();

		////tick
		tickFrame(false);
	}
}

void HavokWidget::treeSelectionChanged(const QModelIndex& current, const QModelIndex& previous)
{
	if (_model->nodeType(current) == NodeType::SkeletonHkxNode && _model->variant(current)->m_class == &hkaSkeletonClass)
	{
		auto pSkel = reinterpret_cast<hkaSkeleton*>(_model->variant(current)->m_object);
		if (pSkel != skeleton)
		{
			skeleton = pSkel;
			m_skelNeedsUpdate = true;
		}
	}
}