# Health faces

The seven guest face PNGs were downloaded unchanged from the RollerCoaster Tycoon wiki's
[Guest Moods](https://rct.fandom.com/wiki/Guest_Moods) page on 2026-10-23. Original game art is from
RollerCoaster Tycoon; this attribution does not relicense that art as project code.

| Asset | Wiki file |
|---|---|
| very-happy.png | https://rct.fandom.com/wiki/File:Very_happy.png.png |
| really-happy.png | https://rct.fandom.com/wiki/File:Really_happy.png.png |
| happy.png | https://rct.fandom.com/wiki/File:Happy.png.png |
| neutral.png | https://rct.fandom.com/wiki/File:Neutral.png.png |
| sad.png | https://rct.fandom.com/wiki/File:Sad.png.png |
| tired.png | https://rct.fandom.com/wiki/File:Tired.png.png |
| very-tired.png | https://rct.fandom.com/wiki/File:Very_tired.png.png |

`dead.png` is a transparent skull sprite made with the built-in imagegen tool, originally using the
seven faces as a style reference. It was revised on 2026-10-24 into a bare skull with hollow sockets,
pronounced cheekbones and a narrow toothy jaw; the [edit prompt](dead-prompt.txt) is saved here.
It displays at 48 by 50dp, centered in the living faces' 54 by 50dp space, keeping player text aligned.
The original downloaded PNGs remain intact. Native previews decode PNGs
with stb_image and premultiply alpha, matching the game's WIC texture convention.

The face represents health, not a separate mood or tiredness statistic. Live thresholds:
90–100% Very Happy; 75–<90% Really Happy; 60–<75% Happy; 45–<60% Neutral; 30–<45% Sad;
15–<30% Tired; >0–<15% Very Tired; zero Dead. Unavailable health uses a dim neutral face and
“Loading”, never a dead face. Names and levels remain text.
