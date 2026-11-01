Import("env")
from SCons.Script import COMMAND_LINE_TARGETS

# Nesta placa o bootloader que o PlatformIO grava entra no loop
# "try 0x4008059c". Para o `pio run -t upload` continuar a ser o comando
# normal, mas gravar só a app em 0x10000, removemos as imagens extra
# (bootloader, tabela de partições e boot_app0) do comando do esptool.
# O bootloader/partições têm de ser os da recovery, que já funcionam.

if env["PIOENV"] in ("w5500_sem_int", "w5500_diag") and "upload" in COMMAND_LINE_TARGETS:
    images = env.get("FLASH_EXTRA_IMAGES", [])
    if images:
        flags = env.get("UPLOADERFLAGS", [])
        # Cada imagem extra ocupa dois argumentos: endereço + ficheiro.
        for _ in images:
            if len(flags) >= 2:
                del flags[-2:]
        env.Replace(UPLOADERFLAGS=flags)
        print("*** upload: só a app; bootloader/partições mantidos ***")
