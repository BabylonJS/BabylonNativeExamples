/// <reference path="../node_modules/babylonjs/babylon.module.d.ts" />
/// <reference path="../node_modules/babylonjs-loaders/babylonjs.loaders.module.d.ts" />

let engine = null;
let scene = null;
let outputTexture = null;
let rootMesh = null;

/**
 * Sets up the engine, scene, and output texture.
 */
function startup(nativeTexture, width, height) {
    // Create a new native engine.
    engine = new BABYLON.NativeEngine();

    // Create a scene with a white background.
    scene = new BABYLON.Scene(engine);

    // Wrap the input native texture in a render target texture for the output
    // render target of the camera used in `loadAndRenderAssetAsync` below.
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

    // Create a default camera that looks at the asset from a specific angle
    // and outputs to the render target created in `startup` above.
    scene.createDefaultCamera(true, true);
    scene.createDefaultLight(true);
    scene.activeCamera.alpha = 2;
    scene.activeCamera.beta = 1.25;
    scene.activeCamera.outputRenderTarget = outputTexture;

    var cube = BABYLON.Mesh.CreateBox("box1", 0.2, scene);

    engine.runRenderLoop(function () {
        cube.rotate(BABYLON.Vector3.Up(), 0.01);
        scene.render();
    });
}
