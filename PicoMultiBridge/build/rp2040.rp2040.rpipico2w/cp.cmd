@for %%f in (*.uf2) do cpybyvol.exe -d RP2350 -n 5 -s %%f
@timeout 2 > nul