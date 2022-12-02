/// <reference path="../node_modules/babylonjs/babylon.module.d.ts" />
/// <reference path="../node_modules/babylonjs-loaders/babylonjs.loaders.module.d.ts" />

let engine = null;
let scene = null;
let outputTexture = null;
let rootMesh = null;

function startup(nativeTexture, width, height) {
    engine = new BABYLON.NativeEngine();

    scene = new BABYLON.Scene(engine);
    scene.clearColor = BABYLON.Color3.White();

    scene.createDefaultEnvironment({ createSkybox: false, createGround: false });

    outputTexture = new BABYLON.RenderTargetTexture(
        "outputTexture",
        {
            width: width,
            height: height
        },
        scene,
        {
            colorAttachment: engine.wrapNativeTexture(nativeTexture),
            generateDepthBuffer: true,
            generateStencilBuffer: true
        }
    );
}

async function loadAndRenderAssetAsync(name, url) {
    if (rootMesh) {
        rootMesh.dispose();
    }

    const result = await BABYLON.SceneLoader.ImportMeshAsync(null, url, undefined, scene);
    rootMesh = result.meshes[0];

    scene.createDefaultCamera(true, true);
    scene.activeCamera.alpha = 2;
    scene.activeCamera.beta = 1.25;
    scene.activeCamera.outputRenderTarget = outputTexture;

    await scene.whenReadyAsync();

    scene.render();
}
