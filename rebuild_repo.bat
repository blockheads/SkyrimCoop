rmdir /s /q "C:\Users\Jigglypuff Main\AppData\Local\.xmake"
set XMAKE_GLOBALDIR=C:\projects\SkyrimCoop
xmake f --plat=windows --arch=x64 --vs=2022 --vs_toolset=14.44 -c -y -m debug
xmake -y