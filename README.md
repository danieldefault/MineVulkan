# VoxelCraft

![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white)
![Vulkan 1.0](https://img.shields.io/badge/Vulkan-1.0-AC162C?logo=vulkan&logoColor=white)
![Android 7.0+](https://img.shields.io/badge/Android-7.0%2B%20(arm64)-3DDC84?logo=android&logoColor=white)
![Linux](https://img.shields.io/badge/Linux-X11%20%2F%20XWayland-FCC624?logo=linux&logoColor=black)

Um clone de Minecraft escrito do zero em **C++17 + Vulkan 1.0**, pensado para rodar bem em **GPUs mobile** (Mali, Adreno).

- **Android:** `NativeActivity` pura, sem uma linha de Java/Kotlin.
- **Linux desktop:** versão X11 para iterar rápido no PC, com o mesmo código de jogo e renderização.
- **Zero assets:** texturas são geradas proceduralmente e os shaders SPIR-V são embutidos no binário.

---

## ✨ Funcionalidades

**Marco 1 — núcleo jogável**

- 🌍 **Mundo infinito** em chunks de 16×16×128, gerado por ruído
- 🏞️ **Biomas** definidos por temperatura, umidade e altitude:

  | Bioma | Superfície | Vegetação |
  |---|---|---|
  | Planície | grama | carvalhos esparsos |
  | Floresta | grama | carvalhos densos |
  | Deserto | areia sobre arenito | cactos |
  | Tundra | terra com neve | pinheiros |
  | Montanha | pedra; neve acima da linha de neve | — |
  | Oceano | fundo de areia, coberto de água até o nível do mar | — |

  Encostas íngremes expõem pedra em qualquer bioma (exceto deserto). Costas têm praias de areia.
- 🌊 **Água translúcida**: oceanos e lagos no nível do mar (y = 44), fundo escurecendo com a profundidade, névoa azul debaixo d'água
- 🏊 **Natação**: afunda devagar, pulo nada para cima, impulso automático para sair pela margem; quebrar um bloco ao lado da água abre espaço para ela (sem simulação de escoamento)
- 📦 **Streaming de chunks** ao redor do jogador, mais próximos primeiro, com orçamento de geração/malha por frame
- 💾 **Edições preservadas**: blocos quebrados/colocados continuam lá ao se afastar e voltar (durante a sessão)
- 🧱 **Malha otimizada** com face culling e oclusão de ambiente por vértice (AO "estilo Minecraft")
- 🏃 **Física do jogador**: AABB, gravidade, pulo, colisão eixo a eixo e pulo automático em degraus
- 🖥️ **Câmera interpolada**: física fixa em 60 Hz, imagem suave em telas de 90/120 Hz
- ⛏️ **Quebrar e colocar blocos** via raycast DDA, com hotbar de 8 blocos
- 🎨 **Texturas 16×16 procedurais** em um texture array com mipmaps
- 🌫️ **Névoa na distância** e **frustum culling** por chunk
- 📱 **Controles touch**: joystick virtual, arrastar para olhar, botão de pulo

Blocos na hotbar: grama, terra, pedra, pedregulho, tábuas, tronco, folhas, areia, arenito e neve. Também aparecem no mundo: terra com neve, cacto, folhas de pinheiro, água e bedrock (indestrutível).

---

## 🚀 Decisões de performance para GPU mobile

| Decisão | Por quê |
|---|---|
| Vértice de 8 bytes (`R8G8B8A8_UINT` ×2) | Menos banda de memória, que é o gargalo em Mali/Adreno |
| Um index buffer compartilhado por todos os quads | Nenhum índice por chunk; upload de malha só copia vértices |
| Depth `TRANSIENT` + memória `LAZILY_ALLOCATED`, `storeOp = DONT_CARE` | Em GPUs tile-based o depth nunca sai da memória on-chip |
| Pré-rotação do swapchain (`preTransform = currentTransform`) | Evita que o compositor do Android gire cada frame |
| Present mode `FIFO` (vsync) | Sempre disponível, estável e economiza bateria/temperatura |
| Texture array em vez de atlas | Mipmaps sem "vazamento" entre texturas vizinhas |
| Chunks desenhados da frente para trás | O early-Z descarta fragmentos escondidos antes do fragment shader (menos overdraw) |
| AABB de culling com a altura real da malha | Chunks abaixo/acima da câmera são descartados mais cedo |
| Água no fim do mesmo vertex buffer do chunk, desenhada num 2º passe | Sem buffer extra; só a faixa de índices muda. Blend só onde há água, de trás para frente |

---

## 🎮 Controles

| Ação | Android (touch) | Desktop |
|---|---|---|
| Mover | Joystick à esquerda | `W` `A` `S` `D` |
| Olhar | Arrastar na tela | Mouse (clique para capturar, `Esc` libera) |
| Pular / nadar para cima | Botão de pulo | `Espaço` |
| Colocar bloco | Toque curto | Botão direito |
| Quebrar bloco | Segurar | Botão esquerdo |
| Trocar bloco | Toque na hotbar | `1`–`9`, `0` ou roda do mouse |

---

## 📲 Gerar o APK

### Opção A — GitHub Actions (sem instalar nada)

O workflow [`.github/workflows/android.yml`](.github/workflows/android.yml) compila um APK release a cada push.
Em paralelo, [`.github/workflows/desktop.yml`](.github/workflows/desktop.yml) compila a versão Linux, roda os testes do núcleo e renderiza 300 frames com a camada de validação do Vulkan (GPU por software), falhando se houver qualquer erro de validação.

1. Faça um fork ou envie o projeto para um repositório seu.
2. Abra **Actions → (última execução) → Artifacts**.
3. Baixe `voxelcraft-apk` no celular e instale.

> O APK release é assinado com a chave de debug, então pode ser instalado diretamente (ative "instalar apps de fontes desconhecidas").

### Opção B — no PC

**Requisitos:** JDK 17+ e Android SDK (com `cmdline-tools`). O Gradle baixa o NDK e o CMake automaticamente na primeira vez.

```bash
export ANDROID_HOME=$HOME/Android/Sdk
./gradlew assembleRelease
adb install -r app/build/outputs/apk/release/app-release.apk
adb logcat -s VoxelCraft      # FPS, chunks e posição a cada 2 s
```

> ⚠️ Use **release** para medir desempenho: o build debug do código nativo sai sem otimização (`-O0`) e fica bem mais lento.

**Aparelho:** Android 7.0+ (API 24), `arm64-v8a`, com suporte a Vulkan 1.0.

---

## 🐧 Versão desktop (Linux)

**Requisitos:** CMake ≥ 3.22, compilador C++17, headers do Vulkan, `glslc` ou `glslangValidator` e headers da `libxcb` (funciona em X11 ou Wayland via XWayland).

<details>
<summary>Instalando as dependências</summary>

```bash
# Fedora
sudo dnf install cmake ninja-build gcc-c++ vulkan-headers vulkan-loader-devel glslc libxcb-devel

# Ubuntu / Debian
sudo apt install cmake ninja-build g++ libvulkan-dev glslc libxcb1-dev
```

</details>

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/core_tests                 # testes do núcleo
./build/voxelcraft
VC_VALIDATION=1 ./build/voxelcraft # com a camada de validação do Vulkan (se instalada)
```

**Opções de linha de comando:**

| Opção | Descrição |
|---|---|
| `--radius N` | Distância de renderização em chunks (padrão: 8) |
| `--seed N` | Semente do mundo (padrão: 1337) |
| `--touch-ui` | Mostra a HUD touch no PC |
| `--spawn X Z` | Começa na terra firme mais próxima de (X, Z) — útil para visitar um bioma (o log mostra o bioma atual) |
| `--look YAW PITCH` | Direção inicial da câmera, em radianos |
| `--frames N` | Encerra após N frames (testes automatizados) |
| `--walk` / `--turn R` | Anda para frente / gira a câmera R rad por frame sozinho |

---

## 🗂️ Estrutura do projeto

```
src/core/      mundo, ruído, mesher, física, texturas, controles touch (sem GPU, testável)
src/render/    renderer Vulkan
src/game/      liga mundo + jogador + renderer, independente de plataforma
src/platform/  android/ (NativeActivity) e desktop/ (X11/xcb)
shaders/       GLSL, compilado para SPIR-V e embutido no binário durante o build
cmake/         scripts de compilação e embutimento dos shaders
tests/         testes do núcleo (terreno, biomas, vegetação, água, natação, mesher, raycast, física, texturas, streaming, edições)
app/           módulo Android (Gradle)
```

O mesmo `CMakeLists.txt` é usado nas duas plataformas: no Android ele é chamado pelo Gradle via `externalNativeBuild`.

---

## 🔧 Ajustes rápidos

| O quê | Onde |
|---|---|
| Distância de renderização no Android | `cfg.renderRadius` em [`src/platform/android/android_main.cpp`](src/platform/android/android_main.cpp) |
| Chunks gerados/remalhados por frame | `GameConfig` em [`src/game/game.h`](src/game/game.h) |
| Imagem girada em algum aparelho | `kUsePreRotation = false` em [`src/render/renderer.cpp`](src/render/renderer.cpp) |
| Sensibilidade do toque | `kLookSensitivity` em [`src/core/touch_controls.cpp`](src/core/touch_controls.cpp) |
| Blocos da hotbar | `kHotbar` em [`src/core/block.h`](src/core/block.h) |
| Limiares/densidade dos biomas | `World::biomeAt` e `placeVegetationInto` em [`src/core/world.cpp`](src/core/world.cpp) |

---

## 📝 Aviso

Projeto de estudo, sem relação com a Mojang ou a Microsoft. "Minecraft" é marca registrada de seus respectivos donos.
