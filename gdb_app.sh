rm -rf ./gdb_init

APP=minied

echo "# qemuに接続
target remote localhost:1234

echo Debug $APP\\n
file ./apps/$APP/$APP

# 逆アセンブル結果をintel形式に
set disassembly-flavor intel

# ブレークするごとに逆アセンブル結果を表示
disp/3i \$pc
" >> ./gdb_init


# echo "# IntHandlerDE" >> ./gdb_init
# nm -C ./kernel/kernel.elf | grep IntHandlerDE | awk '{printf "b *0x%s", $1}' >> ./gdb_init
# echo "" >> ./gdb_init

# echo "# IntHandlerDB" >> ./gdb_init
# nm -C ./kernel/kernel.elf | grep IntHandlerDB | awk '{printf "b *0x%s", $1}' >> ./gdb_init
# echo "" >> ./gdb_init

echo "# IntHandlerBP" >> ./gdb_init
nm -C ./kernel/kernel.elf | grep IntHandlerBP | awk '{printf "b *0x%s", $1}' >> ./gdb_init
echo "" >> ./gdb_init

# echo "# IntHandlerOF" >> ./gdb_init
# nm -C ./kernel/kernel.elf | grep IntHandlerOF | awk '{printf "b *0x%s", $1}' >> ./gdb_init
# echo "" >> ./gdb_init

# echo "# IntHandlerBR" >> ./gdb_init
# nm -C ./kernel/kernel.elf | grep IntHandlerBR | awk '{printf "b *0x%s", $1}' >> ./gdb_init
# echo "" >> ./gdb_init

echo "# IntHandlerUD" >> ./gdb_init
nm -C ./kernel/kernel.elf | grep IntHandlerUD | awk '{printf "b *0x%s", $1}' >> ./gdb_init
echo "" >> ./gdb_init

# echo "# IntHandlerNM" >> ./gdb_init
# nm -C ./kernel/kernel.elf | grep IntHandlerNM | awk '{printf "b *0x%s", $1}' >> ./gdb_init
# echo "" >> ./gdb_init

# echo "# IntHandlerDF" >> ./gdb_init
# nm -C ./kernel/kernel.elf | grep IntHandlerDF | awk '{printf "b *0x%s", $1}' >> ./gdb_init
# echo "" >> ./gdb_init

# echo "# IntHandlerTS" >> ./gdb_init
# nm -C ./kernel/kernel.elf | grep IntHandlerTS | awk '{printf "b *0x%s", $1}' >> ./gdb_init
# echo "" >> ./gdb_init

# echo "# IntHandlerNP" >> ./gdb_init
# nm -C ./kernel/kernel.elf | grep IntHandlerNP | awk '{printf "b *0x%s", $1}' >> ./gdb_init
# echo "" >> ./gdb_init

# echo "# IntHandlerSS" >> ./gdb_init
# nm -C ./kernel/kernel.elf | grep IntHandlerSS | awk '{printf "b *0x%s", $1}' >> ./gdb_init
# echo "" >> ./gdb_init

echo "# IntHandlerGP" >> ./gdb_init
nm -C ./kernel/kernel.elf | grep IntHandlerGP | awk '{printf "b *0x%s", $1}' >> ./gdb_init
echo "" >> ./gdb_init

# echo "# IntHandlerPF" >> ./gdb_init
# nm -C ./kernel/kernel.elf | grep IntHandlerPF | awk '{printf "b *0x%s", $1}' >> ./gdb_init
echo "" >> ./gdb_init

# echo "# IntHandlerMF" >> ./gdb_init
# nm -C ./kernel/kernel.elf | grep IntHandlerMF | awk '{printf "b *0x%s", $1}' >> ./gdb_init
# echo "" >> ./gdb_init

# echo "# IntHandlerAC" >> ./gdb_init
# nm -C ./kernel/kernel.elf | grep IntHandlerAC | awk '{printf "b *0x%s", $1}' >> ./gdb_init
# echo "" >> ./gdb_init

# echo "# IntHandlerMC" >> ./gdb_init
# nm -C ./kernel/kernel.elf | grep IntHandlerMC | awk '{printf "b *0x%s", $1}' >> ./gdb_init
# echo "" >> ./gdb_init

# echo "# IntHandlerXM" >> ./gdb_init
# nm -C ./kernel/kernel.elf | grep IntHandlerXM | awk '{printf "b *0x%s", $1}' >> ./gdb_init
# echo "" >> ./gdb_init

# echo "# IntHandlerVE" >> ./gdb_init
# nm -C ./kernel/kernel.elf | grep IntHandlerVE | awk '{printf "b *0x%s", $1}' >> ./gdb_init
# echo "" >> ./gdb_init

gdb -x gdb_init
