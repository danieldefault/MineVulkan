# VoxelCraft

Clone de Minecraft em **C++17 + Vulkan 1.0**, feito para Android (NativeActivity, sem Java/Kotlin) e com uma versão desktop Linux (X11) para iterar rápido no PC.

## Estado atual — Marco 1 (núcleo jogável)

- Mundo infinito em chunks 16×16×128, gerado por ruído (planícies, colinas, montanhas, desertos, árvores)
- Streaming de chunks ao redor do jogador (mais próximos primeiro, com orçamento por frame)
- Malha com face culling + oclusão de ambiente por vértice (AO "estilo Minecraft")
- Física do jogador (AABB, gravidade, pulo, colisão eixo a eixo, pulo automático)
- Quebrar e colocar blocos (raycast DDA), hotbar com 8 blocos
- Texturas 16×16 geradas proceduralmente (texture array + mipmaps, sem arquivos de imagem)
- Névoa na distância, frustum culling por chunk
- Controles touch: joystick à esquerda, arrastar para olhar, toque curto = colocar, segurar = quebrar, botão de pulo

### Decisões voltadas a GPU mobile

| Decisão | Por quê |
|---|---|
| Vértice de 8 bytes (`R8G8B8A8_UINT` ×2) | Menos banda de memória, que é o gargalo em Mali/Adreno |
| Um index buffer compartilhado para todos os quads | Nenhum índice por chunk; upload de malha só copia vértices |
| Profundidade `TRANSIENT` + memória `LAZILY_ALLOCATED`, `storeOp = DONT_CARE` | Em GPUs tile-based o depth nunca sai da memória on-chip |
| Pré-rotação do swapchain (`preTransform = currentTransform`) | Evita que o compositor do Android gire cada frame |
| `FIFO` (vsync) | Sempre disponível, estável e economiza bateria/temperatura |
| Texture array em vez de atlas | Mipmaps sem "vazamento" entre texturas vizinhas |

## Estrutura

```
src/core/      mundo, ruído, mesher, física, texturas, controles touch (sem GPU, testável)
src/render/    renderer Vulkan
src/game/      liga mundo + jogador + renderer
src/platform/  android/ (NativeActivity) e desktop/ (X11)
shaders/       GLSL, compilado para SPIR-V e embutido no binário durante o build
tests/         testes do núcleo
app/           módulo Android (Gradle)
```

## Gerar o APK

### Opção A — GitHub Actions (sem instalar nada)

1. Crie um repositório no GitHub e envie esta pasta.
2. O workflow `.github/workflows/android.yml` roda a cada push.
3. Em **Actions → (execução) → Artifacts**, baixe `voxelcraft-apk` no celular e instale.

### Opção B — no PC

Requisitos: JDK 17+, Android SDK (com `cmdline-tools`). O Gradle baixa NDK e CMake automaticamente na primeira vez.

```bash
export ANDROID_HOME=$HOME/Android/Sdk
./gradlew assembleRelease
adb install -r app/build/outputs/apk/release/app-release.apk
adb logcat -s VoxelCraft      # FPS, chunks e posição a cada 2 s
```

Use **release** para medir desempenho: o build debug do código nativo sai sem otimização (-O0) e fica bem mais lento.

## Versão desktop (Linux)

Requisitos: CMake ≥ 3.22, compilador C++17, headers do Vulkan, `glslc` ou `glslangValidator`, headers da libxcb (X11 ou Wayland via XWayland).

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/core_tests
./build/voxelcraft                 # clique para capturar o mouse, Esc libera
VC_VALIDATION=1 ./build/voxelcraft # com camada de validação (se instalada)
```

Controles: WASD, espaço, mouse, botão esquerdo quebra, direito coloca, 1–8 ou roda selecionam o bloco.

Opções: `--radius N` (distância em chunks), `--seed N`, `--touch-ui` (mostra a HUD touch no PC).

## Ajustes rápidos

| O quê | Onde |
|---|---|
| Distância de renderização no Android | `cfg.renderRadius` em `src/platform/android/android_main.cpp` |
| Chunks gerados/remalhados por frame | `GameConfig` em `src/game/game.h` |
| Imagem girada em algum aparelho | `kUsePreRotation = false` em `src/render/renderer.cpp` |
| Sensibilidade do toque | `kLookSensitivity` em `src/core/touch_controls.cpp` |
