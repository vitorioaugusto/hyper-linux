# Hyper Linux

[English](README.md) | **Português (Brasil)**

> [!IMPORTANT]
> **Todo este projeto foi vibecodado com assistência de IA.** O código foi construído de forma iterativa por meio de prompts, testes, depuração e refinamento, em vez de um fluxo tradicional de programação do zero.

Aplicativo nativo para controlar o **HyperX Cloud III Wireless** no Linux, feito com Qt 6, Python e PipeWire. Não depende de Wine, NGENUITY ou HyperHeadset.

**Estado:** experimental, testado no CachyOS com KDE Plasma e dongle `03f0:05b7`. Bateria, mute e retorno do microfone foram confirmados nos testes. A variante `03f0:0c9d` é reconhecida, mas ainda não foi testada em hardware aqui. A saída de áudio do equalizador precisa de mais validação.

Projeto independente da comunidade, sem afiliação com HyperX ou HP. Usa ilustrações próprias e fontes do sistema. Não distribui logotipos, fotos de produtos, fontes, instaladores, DLLs ou firmware do fabricante.

![Hyper Linux interface](docs/screenshot.png)

## Recursos

- Bateria e carregamento consultados diretamente no dongle.
- Controle único de mute, acompanhando os eventos de áudio do Linux e do botão físico.
- Sidetone do fone e retorno separado pelo PipeWire, com volume ajustável.
- Desligamento automático, volume de saída e ganho do microfone.
- Equalizador por software com 10 bandas, frequência e Q ajustáveis, presets e perfis JSON.
- Diagnóstico local e registro de comandos USB, sem gravação de áudio.

## Dependências

É necessária uma sessão de desktop com PipeWire e sua compatibilidade com PulseAudio.

### Arch / CachyOS

```bash
sudo pacman -S --needed gcc pkgconf qt6-base python pipewire pipewire-audio pipewire-pulse libpulse libusb
```

### Ubuntu / Debian

```bash
sudo apt install g++ pkg-config qt6-base-dev python3 pipewire pipewire-bin pipewire-pulse pulseaudio-utils libusb-1.0-0
```

A disponibilidade dos pacotes e a configuração dos serviços variam por distribuição. Apenas instalar os pacotes não substitui um servidor de áudio já em execução.

## Compilar e abrir

```bash
git clone https://github.com/vitorioaugusto/hyper-linux.git
cd hyper-linux
bash build.sh
bash run.sh
```

Execute como usuário normal do desktop, sem `sudo`. Feche outros aplicativos de controle do fone durante os testes. Se baixar um arquivo compilado de uma execução bem-sucedida do GitHub Actions, basta extrair e executar `bash run.sh`, sem precisar do compilador.

### Instalar no seu usuário

```bash
bash install.sh
```

Instala em `~/.local/share/hyper-linux`, cria o launcher em `~/.local/bin` e adiciona **Hyper Linux** ao menu de aplicativos. Respeita `XDG_DATA_HOME`. Não instala dependências nem altera os serviços de áudio do sistema.

### Permissão USB, se necessária

Caso o diagnóstico indique acesso negado a `/dev/hidraw*`:

```bash
sudo install -m644 udev/70-hyper-linux.rules /etc/udev/rules.d/70-hyper-linux.rules
sudo udevadm control --reload-rules
```

Desconecte e reconecte o dongle. As regras concedem acesso ao usuário local ativo; o aplicativo continua sendo executado sem `sudo`.

## Retorno do microfone

Em **Dispositivo**, ative **Ouvir minha voz no fone** para usar o sidetone de hardware. Se não ouvir o retorno, desligue essa opção e teste **Áudio → Ativar retorno por software**. Comece em 25% e use somente uma modalidade por vez para evitar eco.

O retorno por software solicita 10 ms ao PipeWire; a latência final depende do computador e do fone e não foi medida. Fechar o app encerra os processos de retorno e equalização criados por ele.

## Equalizador

Selecione a saída física do fone em **Áudio** e ative o efeito em **Equalizador**. Nas configurações de som do desktop, escolha **Hyper Linux · Equalizador** como saída. O aplicativo não troca o dispositivo padrão automaticamente.

As bandas positivas aplicam uma margem conservadora para reduzir distorção, podendo diminuir o volume geral. Os perfis usam o formato JSON `hyper-linux-eq-v1`. O efeito é processado no Linux e não fica salvo na memória do fone.

## Limitações

- Sem DTS/SONAMI, processamento proprietário de voz por IA ou atualização de firmware.
- Não presume compatibilidade com o Cloud III S nem com o Cloud III com fio.
- Sem ajuste de nível do sidetone ou EQ embarcado no Cloud III Wireless original.
- Aplicar edições no EQ recria o nó do filtro; pode ser necessário selecionar novamente sua saída.

## Diagnóstico e testes

```bash
python3 backend.py --diagnose
python3 -m unittest -v test_backend
QT_QPA_PLATFORM=offscreen ./hyper-linux --preview --smoke-test
```

O diagnóstico faz somente consultas. Os logs ficam em `~/.local/state/hyper-linux`, respeitando `XDG_STATE_HOME`. Revise os arquivos antes de anexá-los a uma issue: podem conter identificadores locais de dispositivos e caminhos de arquivos. Informe distribuição, desktop, ID USB do dongle e o comportamento observado.

Os [detalhes do protocolo](docs/protocol.md) descrevem os pacotes e a confirmação dos comandos.

## Licença

O código e a arte original usam a [licença MIT](LICENSE). Qt e outras dependências do sistema mantêm suas próprias licenças. Os nomes dos produtos identificam o hardware compatível.
