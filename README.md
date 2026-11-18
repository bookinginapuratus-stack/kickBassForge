# KickBassForge

Plugin VST3 que gera kick + baixo sintetizados, com sidechain automatico embutido
(o kick gerado aciona o ducking do baixo). A analise de uma musica de referencia
e feita pelo script `tools/analyze.py`, que gera um preset JSON carregado no plugin.

## 1. Compilar o plugin
Requisitos: CMake 3.22+, Visual Studio 2022 (Windows) ou Xcode (macOS), Git.

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

O VST3 fica em `build/KickBassForge_artefacts/Release/VST3/`.
- Windows: copie para `C:\Program Files\Common Files\VST3\`
- macOS: `~/Library/Audio/Plug-Ins/VST3/`
No Ableton: Preferences > Plug-ins > ative VST3 e rescaneie. Coloque em uma faixa MIDI e de play.

Sem compilador local: suba a pasta para o GitHub; a aba Actions compila Windows e macOS.

## 2. Analisar uma musica
    pip install -r tools/requirements.txt
    python tools/analyze.py minha_musica.mp3 -o preset.json

No plugin, clique em **Carregar analise (.json)**. Os parametros do kick (pitch, decay,
click, drive) e do baixo (cutoff, sub, drive) sao ajustados, e o padrao ritmico e as
notas aprendidas ficam salvos.

## 3. Variacoes
Ajuste o knob **Variacao** (0 = igual ao original, 1 = totalmente novo, mas no estilo
das probabilidades aprendidas) e clique em **Gerar variacao**.

## Sidechain
Duck Depth e Duck Release controlam a profundidade e o release do ducking do baixo.

## Proximos passos
- Exportar o padrao como MIDI / audio
- Rodar o Demucs (ONNX) dentro do plugin
- Gerador de padroes com modelo de sequencia (LSTM/Transformer pequeno)
