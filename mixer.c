#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>

#define BSET(mask, ibit) (mask|=1<<ibit)
#define BUNSET(mask, ibit) (mask&=~(1<<ibit))
#define ISBSET(mask, ibit) ((mask&(1<<ibit))>0)

enum {
	Oplen = 4, /* strlen("drop") */
	GridPx = 1,
	HandlePx= 3,
	Nchans = SOUND_MIXER_NRDEVICES,
	VolDelta = 2,
	VolMax = 100,
	Etimer = Ekeyboard << 1,
	PollMsec = 1000,
};

enum {
	Grid,
	Handle,
	SlideBg,
	SlideFg,
	Text,
	Ncolors,
};

Font *labelfont;
Image *colors[Ncolors];
Menu menu;
int enabled;
int mcounter;
int mixer;
int nenabled;
int sheight;
int supported;
int volumes[Nchans];
char *dev = "/dev/mixer";
char *menustr[Nchans+1];
char *names[] = SOUND_DEVICE_NAMES;

int
eprint(char *fmt, ...) 
{
	va_list v;
	int b;
	
	b = fprint(2, "mixer: ");
	va_start(v, fmt);
	b += vfprint(2, fmt, v);
	va_end(v);
	return b;
}

int
max(int a, int b)
{
	return b>a ? b : a;
}

int
min(int a, int b)
{
	return b<a ? b : a;
}

void
loadvols()
{
	int i, vol;
	for (i=0; i<Nchans; i++) {
		if (!ISBSET(supported, i)) {
			continue;
		}
		if (ioctl(mixer, MIXER_READ(i), &vol) <0) {
			eprint("could not load volume for channel %s: %r\n", names[i]);
			BUNSET(supported, i);
			continue;
		}
		/*
		 * Just ignore stereo property: it's overkill. Note that
		 * the left value (least significant byte) is set whether
		 * the channel is either mono or stereo, so we will use that.
		 */
		volumes[i] = vol & 0xFF;
	}
}

int
setvol(int ch, int vol)
{
	int v;
	
	/* threat channel as stereo, just in case */
	v =  vol<<8 | vol;
	if (ioctl(mixer, MIXER_WRITE(ch), &v) <0) {
		return -1;
	}
	mcounter++;
	volumes[ch] = vol;
	return 0;
}

/* Return the position of the nth true bit from mask (LSB 0) */
int
getchan(int n, int mask)
{
	int i;
	for (i=0; i<Nchans; i++) {
		if (ISBSET(mask, i)) {
			if (n-- == 0) {
				return i;
			}
		}
	}
	return -1;	
}

int
mselect(int n)
{
	int c;
	
	c = getchan(n, supported);
	if (c < 0) {
		return -1;
	}
	if (ISBSET(enabled, c)) {
		if (nenabled == 1) {
			return -1;
		}
		BUNSET(enabled, c);
		nenabled--;
		memmove(menustr[n], "add ", Oplen);
	} else {
		BSET(enabled, c);
		nenabled++;
		memmove(menustr[n], "drop", Oplen);
	}
	return 0;
}

void
drawslider(int ch, int pos)
{
	char buf[16];
	Rectangle r, tmp;
	
	/* foreground */
	r.min.y = sheight*pos + GridPx;
	r.min.x = GridPx;
	if (pos < nenabled - 1) {
		r.max.y = r.min.y + sheight - GridPx;
	} else {
		r.max.y = screen->r.max.y - GridPx;
	}
	r.max.x = (screen->r.max.x - GridPx) * volumes[ch] / VolMax;
	draw(screen, r, colors[SlideFg], nil, ZP);
	
	/* handle */
	tmp = r;
	tmp.min.x = max(r.max.x - HandlePx, GridPx);
	draw(screen, tmp, colors[Handle], nil, ZP);

	/* background */
	tmp = r;
	tmp.min.x = max(r.max.x, GridPx);
	tmp.max.x = screen->r.max.x - GridPx;
	draw(screen, tmp, colors[SlideBg], nil, ZP);

	/* label */
	tmp = r;
	tmp.min.y += GridPx;
	tmp.min.x = GridPx * 2;
	snprint(buf, sizeof(buf), "%s", names[ch]);
	string(screen, tmp.min, colors[Text], ZP, labelfont, buf);
}

void
eresized(int new)
{
	int i, j;
	
	if (new && getwindow(display, Refnone) <0) {
		sysfatal("can't reattach to window: %r");
	}
	draw(screen, screen->r, colors[Grid], nil, ZP);
	sheight = screen->r.max.y / nenabled;
	for (i=0, j=0; j<nenabled; i++) { 
		if (ISBSET(enabled, i)) {
			drawslider(i, j++);
		}
	}
	flushimage(display, 1);
}

void
initcolors()
{
	Rectangle r;
	
	r = Rect(0, 0, 1, 1);
	colors[Grid] = display->black;
	colors[Handle] = allocimage(display, r, CMAP8, 1, DDarkgreen);
	colors[SlideBg] = allocimage(display, r, CMAP8, 1, 0xEAFFEAFF);
	colors[SlideFg] = allocimage(display, r, CMAP8, 1, DMedgreen);
	colors[Text] = display->black;
}

void
initmenu()
{
	char *s, buf[32];
	int i, j;
	for (i=0, j=0; i<Nchans; i++) {
		if (ISBSET(supported, i)) {
			s = ISBSET(enabled, i) ? "drop" : "add ";
			snprint(buf, sizeof(buf), "%s %s", s, names[i]);
			menustr[j] = strdup(buf);
			if (menustr[j] == nil) {
				eprint("strdup: out of memory\n");
				exits("out of memory");
			}
			j++;
		}
	}
	menu.item = menustr;
	menu.lasthit = 0;
}

void
main(int argc, char **argv)
{
	struct mixer_info mi;
	Event e;
	int i, lastpicked, c, p, v, all;
	
	all = 0;
	ARGBEGIN {
	case 'a':
		all++;
		break;
	default:
		fprint(2, "usage: mixer [-a]\n");
		exits("usage");
	} ARGEND

	mixer = open(dev, O_RDWR);
	if (mixer < 0) {
		eprint("could not open %s: %r\n", dev);
		exits("bad mixer file");
	}
	if (ioctl(mixer, SOUND_MIXER_READ_DEVMASK, &supported) <0) {
		eprint("could not load supported channels: %r\n");
		exits("ioctl error");
	}
	if (supported == 0) {
		eprint("no supported channels found.\n");
		exits(nil);
	}
	
	if (initdraw(0, nil, "mixer") <0) {
		sysfatal("initdraw: %r");
	}
	labelfont = openfont(display, "/lib/font/bit/lucsans/boldtypeunicode.7.font");
	if (labelfont == nil) {
		eprint("could not open font: %r\n");
		exits("font");
	}
	initcolors();
	initmenu();
	for (i=0; i<Nchans; i++) {
		if (mselect(i) == 0 && !all) {
			break;
		}
	}
	loadvols();
	eresized(0);
	lastpicked = -1;
	if (etimer(Etimer, PollMsec) == 0) {
		eprint("could not initialize timer: %r\n");
	}
	einit(Emouse|Ekeyboard|Etimer);
	for (;;) {
		switch (event(&e)) {
		case Etimer:
			if (ioctl(mixer, SOUND_MIXER_INFO, &mi) <0) {
				eprint("could not load mixer information: %r\n");
				break;
			}
			if (mcounter < mi.modify_counter || mi.modify_counter == 0) {
				mcounter = mi.modify_counter;
				loadvols();
				eresized(0);
			}
			break;
		case Ekeyboard:
			if (e.kbdc == 0x7F) {
				exits(nil);
			}
			break;
		case Emouse:
			if (e.mouse.buttons == 0) {
				lastpicked = -1;
				break;
			}
			if (ISBSET(e.mouse.buttons, 2)) {
				p = emenuhit(3, &e.mouse, &menu);
				if (mselect(p) == 0) {
					menu.lasthit = p;
					eresized(0);
				}
				break;
			}
			
			p = nenabled * e.mouse.xy.y / screen->r.max.y;
			c = getchan(p, enabled);
			if (c < 0) {
				break;
			}
			if (ISBSET(e.mouse.buttons, 0)) {
				if (lastpicked < 0) {
					lastpicked = c;
				} else if (c != lastpicked) {
					break;
				}
				v = VolMax * e.mouse.xy.x / screen->r.max.x;
			} else if (ISBSET(e.mouse.buttons, 3)) {
				v = volumes[c] + VolDelta;
			} else if (ISBSET(e.mouse.buttons, 4)) {
				v = volumes[c] - VolDelta;
			} else {
				break;
			}
			v = max(v, 0);
			v = min(VolMax, v);
			if (setvol(c, v) <0) {
				eprint("could not set volume %d to channel %d: %r\n", v, c);
				break;
			}
			drawslider(c, p);
			break;
		}
	}
}
