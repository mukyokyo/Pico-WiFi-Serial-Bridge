@for %%f in (*.uf2) do cpybyvol.exe -d RPI-RP2 -n 5 -s %%f
@timeout 2 > nul