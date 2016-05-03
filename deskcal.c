#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>

enum
{
	Etimer = Ekeyboard << 1,
};

char *strwdays[] = {
	"Sunday",
	"Monday",
	"Tuesday",
	"Wednesday",
	"Thursday",
	"Friday",
	"Saturday",
};

char strmday[2+1];
char strclock[2+1+2+1];

Font *mdayfont;
Font *wdayfont;
Font *clockfont;

Tm *now;
int mday;

Font*
eopenfont(Display *d, char *name)
{
	Font *f;
	f = openfont(d, name);
	if (f == nil) {
		fprint(2, "fatal: cannot open font %s: %r\n", name);
		exits("font error");
	}
	return f;
}

void
update(void)
{
	now = localtime(time(0));
	if (!mday || mday != now->mday) {
		mday = now->mday;
		sprint(strmday, "%d", mday);
	}
	sprint(strclock, "%02d:%02d", now->hour, now->min);
}

void
eresized(int new)
{
	Point p, q;
	
	if (new && getwindow(display, Refnone) <0) {
		sysfatal("can't reattach to window: %r");
	}
	draw(screen, screen->r, display->white, nil, ZP);
	
	p = stringsize(wdayfont, strwdays[now->wday]);
	q.y = 0;
	q.x = (screen->r.max.x - p.x) / 2;
	string(screen, q, display->black, ZP, wdayfont, strwdays[now->wday]);

	q.y = p.y;
	p = stringsize(mdayfont, strmday);
	q.x = (screen->r.max.x - p.x) / 2;
	string(screen, q, display->black, ZP, mdayfont, strmday);
	
	q.y += p.y;
	p = stringsize(clockfont, strclock);
	q.x = (screen->r.max.x - p.x) / 2;
	string(screen, q, display->black, ZP, clockfont, strclock);
	
	flushimage(display, 1);
}

void
main()
{
	Event e;
	
	if (initdraw(0, nil, "deskcal") <0) {
		sysfatal("initdraw: %r");
	}

	wdayfont = eopenfont(display, "/lib/font/bit/lucsans/latin1.8.font");
	mdayfont = eopenfont(display, "/lib/font/bit/lucsans/boldunicode.13.font");
	clockfont = eopenfont(display, "/lib/font/bit/pelm/latin1.8.font");
	
	if (etimer(Etimer, 900) == 0) {
		fprint(2, "could not initialize timer: %r\n");
		exits("system error");
	}
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
		}
	}
}
