rm -rf ./gdb_init

echo "# qemuに接続
target remote localhost:1234

file ./kernel/kernel.elf

# 逆アセンブル結果をintel形式に
set disassembly-flavor intel

# ブレークするごとに逆アセンブル結果を表示
disp/5i \$pc
" >> ./gdb_init


echo "# IntHandlerPF" >> ./gdb_init
nm -C ./kernel/kernel.elf | grep IntHandlerPF | awk '{printf "b *0x%s", $1}' >> ./gdb_init
echo "" >> ./gdb_init

echo "# IntHandlerGP" >> ./gdb_init
nm -C ./kernel/kernel.elf | grep IntHandlerGP | awk '{printf "b *0x%s", $1}' >> ./gdb_init
echo "" >> ./gdb_init

echo "# IntHandlerUD" >> ./gdb_init
nm -C ./kernel/kernel.elf | grep IntHandlerUD | awk '{printf "b *0x%s", $1}' >> ./gdb_init
echo "" >> ./gdb_init

echo "#IntHandlerBP" >> ./gdb_init
nm -C ./kernel/kernel.elf | grep IntHandlerBP | awk '{printf "b *0x%s", $1}' >> ./gdb_init
echo "" >> ./gdb_init

gdb -x gdb_init
