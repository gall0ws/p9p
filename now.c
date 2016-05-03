#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>

#define BSET(mask, ibit) (mask|=1<<ibit)
#define BUNSET(mask, ibit) (mask&=~(1<<ibit))
#define ISBSET(mask, ibit) ((mask&(1<<ibit))>0)

enum {
	Etimer = Ekeyboard<<1,
	Oplen = sizeof("drop")-1,
	GridPx = 1,
};

enum {
	Grid,
	BarBg,
	BarFg,
	Text,
	Ncolors,
};

enum {
	Month,
	Day,
	WDay,
	Hour,
	Minute,
	Second,
	NBars,
};

int coef[NBars] = {
	1,
	1, 
	1, 
	1, 
	1, 
	4,
};

int bsize[NBars] = {
	11*31 + 30,
	0, /* Days in a month: will use dmsize */
	6*24 + 23,
	23*60 + 59,
	59*60 + 59,
	59,
};

int dmsize[12] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

char *label[NBars] = {
	"month",
	"mday",
	"wday",
	"hour",
	"min",
	"sec",
};

char *lmon[12] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep",
	"Oct", "Nov", "Dec",
};

char *osufxstr[4] = { 
	"th","st", "nd", "rd",
};

int osufxmap[32] = {
	0, 1, 2, 3, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 1, 2, 3, 0, 0, 0, 0, 0, 0,
	0, 1,
};

#define ORDSUFX(n)	osufxstr[osufxmap[n]]

char *lwday[7] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", 
};

int now[NBars];
int now2[NBars]; /* actual data that will be plotted */
Image *colors[Ncolors];
Menu menu;
char *menustr[NBars+1];
int nslots;
uint slot;
int slotdy;
Font *labelfont;

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
update(void)
{
	Tm* t;
	
	t = localtime(time(nil));
	now[Month] = t->mon;
	now[Day] = t->mday;
	now[WDay] = t->wday;
	now[Hour] = t->hour;
	now[Minute] = t->min;
	now[Second] = t->sec;

	now2[Month] = t->mon*dmsize[t->mon] + t->mday;
	now2[Day] = t->mday*dmsize[t->mon]*24 + t->hour;
	now2[WDay] = t->wday*24 + t->hour;
	now2[Hour] = t->hour*60 + t->min;
	now2[Minute] = t->min*60 + t->sec;
	now2[Second] = t->sec;
	
	bsize[Day] = dmsize[t->mon]*dmsize[t->mon]*24 + 23;
}

int
mselect(int n)
{
	char *op;

	if (ISBSET(slot, n)) {
		if (nslots == 1) {
			return -1;
		}
		BUNSET(slot, n);
		nslots--;
		op = "add ";
	} else {
		BSET(slot, n);
		nslots++;
		op = "drop";
	}
	memcpy(menustr[n], op, Oplen);
	return 0;
}

void
getlabel(int s, char *buf, size_t sz)
{
	int v;
	
	v = now[s];
	switch (s) {
	case Month:
		snprint(buf, sz, lmon[v]);
		break;
	case Day:
		snprint(buf, sz, "%d%s", v, ORDSUFX(v));
		break;
	case WDay:
		snprint(buf, sz, lwday[v]);
		break;
	default:
		snprint(buf, sz, "%02d", v);
	}
}

void
drawslot(int s, int pos)
{
	char buf[16];
	Rectangle r, tmp;
	
	/* foreground */
	r.min.y = slotdy*pos + GridPx;
	r.min.x = GridPx;
	if (pos < nslots - 1) {
		r.max.y = r.min.y + slotdy - GridPx;
	} else {
		r.max.y = screen->r.max.y - GridPx;
	}
	r.max.x = (screen->r.max.x - GridPx) * now2[s] / bsize[s];
	draw(screen, r, colors[BarFg], nil, ZP);
	
	/* background */
	tmp = r;
	tmp.min.x = max(r.max.x, GridPx);
	tmp.max.x = screen->r.max.x - GridPx;
	draw(screen, tmp, colors[BarBg], nil, ZP);

	/* label */
	tmp = r;
	tmp.min.y += GridPx + 2;
	tmp.min.x = GridPx + 4;
	getlabel(s, buf, sizeof(buf));
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
	slotdy = screen->r.max.y / nslots;
	for (i=0, j=0; j<nslots; i++) { 
		if (ISBSET(slot, i)) {
			drawslot(i, j++);
		}
	}
	flushimage(display, 1);
}

void
initcolors()
{
	Rectangle r;
	
	r = Rect(0, 0, 1, 1);

	colors[Grid] = allocimage(display, r, CMAP8, 1, 0xDDDDDDFF);
	colors[BarBg] = allocimage(display, r, CMAP8, 1, 0xEEEEEEFF);
	colors[BarFg] = allocimage(display, r, CMAP8, 1, 0xCCCCCCFF);
	colors[Text] = display->black;
}

void
initmenu()
{
	char *s, buf[32];
	int i;

	for (i=0; i<NBars; i++) {
		s = (ISBSET(slot, i)) ? "drop" : "add ";
		snprint(buf, sizeof(buf), "%s %s", s, label[i]);
		menustr[i] = strdup(buf);
		if (menustr[i] == nil) {
			sysfatal("strdup: out of memory\n");
		}
	}
	menu.item = menustr;
	menu.lasthit = 0;
}

void
main(int argc, char **argv)
{
	Event e;
	int p, i;

	ARGBEGIN {
		case 'M':
			BSET(slot, Month);
			break;
		case 'd':
			BSET(slot, Day);
			break;
		case 'w':
			BSET(slot, WDay);
			break;
		case 'h':
			BSET(slot, Hour);
			break;
		case 'm':
			BSET(slot, Minute);
			break;
		case 's':
			BSET(slot, Second);
			break;
		default:
			fprint(2, "usage: now [-Mdwhms]\n");
			exits("usage");
	} ARGEND;
	if (slot == 0) {
		BSET(slot, Hour);
		BSET(slot, Minute);
		BSET(slot, Second);
		nslots = 3;
	} else {
		for (i=0; i<NBars; i++) {
			if (ISBSET(slot, i)) {
				nslots++;
			}
		}
	}
	if (initdraw(0, nil, "now") < 0) {
		sysfatal("initdraw: %r");
	}
	if (etimer(Etimer, 1000) == 0) {
		sysfatal("could not initialize timer: %r\n");
	}
	labelfont = openfont(display, "/lib/font/bit/lucsans/boldtypeunicode.7.font");
	if (labelfont == nil) {
		sysfatal("could not open font: %r\n");
	}
	initcolors();
	initmenu();
	update();
	eresized(0);
	einit(Emouse|Ekeyboard|Etimer);
	for (;;) {
		switch (event(&e)) {
		case Etimer:
			update();
			eresized(0);
			break;
		case Ekeyboard:
			if (e.kbdc == 0x7F) {
				exits(nil);
			}
			break;
		case Emouse:
			if (ISBSET(e.mouse.buttons, 2)) {
				p = emenuhit(3, &e.mouse, &menu);
				if (p != -1 && mselect(p) == 0) {
					menu.lasthit = p;
					eresized(0);
				}
			}
			break;
		}
	}
}
