/* NetHack 3.7	wish.c	*/
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/*-Copyright (c) Robert Patrick Rankin, 2013. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"
#include "jitems.h"

struct _readobjnam_data {
    struct obj *otmp;
    char *bp;
    char *origbp;
    char oclass;
    char *un, *dn, *actualn;
    const char *name;
    char *p;
    int cnt, spe, spesgn, typ, very, rechrg;
    int blessed, uncursed, iscursed, ispoisoned, isgreased;
    int eroded, eroded2, erodeproof, locked, unlocked, broken, real, fake;
    int halfeaten, mntmp, contents;
    int islit, unlabeled, ishistoric, isdiluted, trapped;
    int doorless, open, closed, looted;
    int tmp, tinv, tvariety, mgend;
    int wetness, gsize;
    int ftype;
    boolean zombify;
    char globbuf[BUFSZ];
    char fruitbuf[BUFSZ];
};

static void wishcmdassist(int);
static struct obj *wizterrainwish(struct _readobjnam_data *);
static void readobjnam_init(char *, struct _readobjnam_data *);
static int readobjnam_preparse(struct _readobjnam_data *);
static void readobjnam_parse_charges(struct _readobjnam_data *);
static int readobjnam_postparse1(struct _readobjnam_data *);
static int readobjnam_postparse2(struct _readobjnam_data *);
static int readobjnam_postparse3(struct _readobjnam_data *);
static short rnd_otyp_by_wpnskill(schar);
static short rnd_otyp_by_namedesc(const char *, char, int);

#define MAXWISHTRY 5

DISABLE_WARNING_FORMAT_NONLITERAL

static void
wishcmdassist(int triesleft)
{
    static NEARDATA const char *
        wishinfo[] = {
  "Wish details:",
  "",
  "Enter the name of an object, such as \"potion of monster detection\",",
  "\"scroll labeled README\", \"elven mithril-coat\", or \"Grimtooth\"",
  "(without the quotes).",
  "",
  "For object types which come in stacks, you may specify a plural name",
  "such as \"potions of healing\", or specify a count, such as \"1000 gold",
  "pieces\", although that aspect of your wish might not be granted.",
  "",
  "You may also specify various prefix values which might be used to",
  "modify the item, such as \"uncursed\" or \"rustproof\" or \"+1\".",
  "Most modifiers shown when viewing your inventory can be specified.",
  "",
  "You may specify 'nothing' to explicitly decline this wish.",
  0,
    },
        preserve_wishless[] = "Doing so will preserve 'wishless' conduct.",
        retry_info[] =
                    "If you specify an unrecognized object name %s%s time%s,",
        retry_too[] = "a randomly chosen item will be granted.",
        suppress_cmdassist[] =
            "(Suppress this assistance with !cmdassist in your config file.)",
        *cardinals[] = { "zero",  "one",  "two", "three", "four", "five" },
        too_many[] = "too many";
    int i;
    winid win;
    char buf[BUFSZ];

    win = create_nhwindow(NHW_TEXT);
    if (!win)
        return;
    for (i = 0; i < SIZE(wishinfo) - 1; ++i)
        putstr(win, 0, wishinfo[i]);
    if (!u.uconduct.wishes)
        putstr(win, 0, preserve_wishless);
    putstr(win, 0, "");
    Sprintf(buf, retry_info,
            (triesleft >= 0 && triesleft < SIZE(cardinals))
               ? cardinals[triesleft]
               : too_many,
            (triesleft < MAXWISHTRY) ? " more" : "",
            plur(triesleft));
    putstr(win, 0, buf);
    putstr(win, 0, retry_too);
    putstr(win, 0, "");
    if (iflags.cmdassist)
        putstr(win, 0, suppress_cmdassist);
    display_nhwindow(win, FALSE);
    destroy_nhwindow(win);
}

RESTORE_WARNING_FORMAT_NONLITERAL

void
makewish(void)
{
    char buf[BUFSZ] = DUMMY;
    char bufcpy[BUFSZ], wish[BUFSZ], promptbuf[QBUFSZ];
    struct obj *otmp, nothing;
    long maybe_LL_arti;
    int tries = 0;
    long oldwisharti = u.uconduct.wisharti;

    promptbuf[0] = '\0';
    nothing = cg.zeroobj; /* lint suppression; only its address matters */
    if (flags.verbose)
        You("may wish for an object.");
 retry:
    Strcpy(promptbuf, "For what do you wish");
    if (iflags.cmdassist && tries > 0)
        Strcat(promptbuf, " (enter 'help' for assistance)");
    Strcat(promptbuf, "?");
    getlin(promptbuf, buf);
    (void) mungspaces(buf);
    if (buf[0] == '\033') {
        buf[0] = '\0';
    } else if (!strcmpi(buf, "help")) {
        wishcmdassist(MAXWISHTRY - tries);
        buf[0] = '\0'; /* for EDIT_GETLIN */
        goto retry;
    }
    /*
     *  Note: if they wished for and got a non-object successfully,
     *  otmp == &zeroobj.  That includes an artifact which has been denied.
     *  Wishing for "nothing" requires a separate value to remain distinct.
     */
    strcpy(bufcpy, buf);
    otmp = readobjnam(buf, &nothing);
    if (!otmp) {
        pline("Nothing fitting that description exists in the game.");
        if (++tries < MAXWISHTRY)
            goto retry;
        pline1(thats_enough_tries);
        otmp = readobjnam((char *) 0, (struct obj *) 0);
        if (!otmp)
            return; /* for safety; should never happen */
    } else if (otmp == &nothing) {
        /* explicitly wished for "nothing", presumably attempting
           to retain wishless conduct */
        livelog_printf(LL_WISH, "declined to make a wish");
        return;
    } else if (otmp == &cg.zeroobj) {
        /* wizard mode terrain wish: skip livelogging, etc */
        return;
    }

    if (otmp->oartifact) {
        /* update artifact bookkeeping; doesn't produce a livelog event */
        artifact_origin(otmp, ONAME_WISH | ONAME_KNOW_ARTI);
    }

    /* wisharti conduct handled in readobjnam() */
    maybe_LL_arti = ((oldwisharti < u.uconduct.wisharti) ? LL_ARTIFACT : 0L);
    Snprintf(wish, sizeof wish, "\"%s\", got \"%s\"", bufcpy, doname(otmp));
    /* KMH, conduct */
    if (!u.uconduct.wishes++)
        livelog_printf((LL_CONDUCT | LL_WISH | maybe_LL_arti),
                       "made %s first wish - %s", uhis(), wish);
    else if (!oldwisharti && u.uconduct.wisharti)
        livelog_printf((LL_CONDUCT | LL_WISH | LL_ARTIFACT),
                       "made %s first artifact wish - %s", uhis(), wish);
    else
        livelog_printf((LL_WISH | maybe_LL_arti), "wished for %s", wish);
    /* TODO? maybe generate a second event decribing what was received since
       those just echo player's request rather than show actual result */

    const char *verb = ((Is_airlevel(&u.uz) || u.uinwater) ? "slip" : "drop"),
               *oops_msg = (u.uswallow
                            ? "Oops!  %s out of your reach!"
                            : (Is_airlevel(&u.uz) || Is_waterlevel(&u.uz)
                               || levl[u.ux][u.uy].typ < IRONBARS
                               || levl[u.ux][u.uy].typ >= ICE)
                               ? "Oops!  %s away from you!"
                               : "Oops!  %s to the floor!");

    /* The(aobjnam()) is safe since otmp is unidentified -dlc */
    (void) hold_another_object(otmp, oops_msg, The(aobjnam(otmp, verb)),
                               (const char *) 0);
    u.ublesscnt += rn1(100, 50); /* the gods take notice */
}

struct o_range {
    const char *name, oclass;
    int f_o_range, l_o_range;
};

/* wishable subranges of objects */
static NEARDATA const struct o_range o_ranges[] = {
    { "bag", TOOL_CLASS, SACK, BAG_OF_TRICKS },
    { "lamp", TOOL_CLASS, OIL_LAMP, MAGIC_LAMP },
    { "candle", TOOL_CLASS, TALLOW_CANDLE, WAX_CANDLE },
    { "horn", TOOL_CLASS, TOOLED_HORN, HORN_OF_PLENTY },
    { "shield", ARMOR_CLASS, SMALL_SHIELD, SHIELD_OF_REFLECTION },
    { "hat", ARMOR_CLASS, FEDORA, DUNCE_CAP },
    { "helm", ARMOR_CLASS, ELVEN_LEATHER_HELM, HELM_OF_TELEPATHY },
    { "gloves", ARMOR_CLASS, LEATHER_GLOVES, GAUNTLETS_OF_DEXTERITY },
    { "gauntlets", ARMOR_CLASS, LEATHER_GLOVES, GAUNTLETS_OF_DEXTERITY },
    { "boots", ARMOR_CLASS, LOW_BOOTS, LEVITATION_BOOTS },
    { "shoes", ARMOR_CLASS, LOW_BOOTS, IRON_SHOES },
    { "cloak", ARMOR_CLASS, MUMMY_WRAPPING, CLOAK_OF_DISPLACEMENT },
    { "shirt", ARMOR_CLASS, HAWAIIAN_SHIRT, T_SHIRT },
    { "dragon scales", ARMOR_CLASS, GRAY_DRAGON_SCALES,
      YELLOW_DRAGON_SCALES },
    { "dragon scale mail", ARMOR_CLASS, GRAY_DRAGON_SCALE_MAIL,
      YELLOW_DRAGON_SCALE_MAIL },
    { "sword", WEAPON_CLASS, SHORT_SWORD, KATANA },
    { "venom", VENOM_CLASS, BLINDING_VENOM, ACID_VENOM },
    { "gray stone", GEM_CLASS, LUCKSTONE, FLINT },
    { "grey stone", GEM_CLASS, LUCKSTONE, FLINT },
};

/* alternate spellings; if the difference is only the presence or
   absence of spaces and/or hyphens (such as "pickaxe" vs "pick axe"
   vs "pick-axe") then there is no need for inclusion in this list;
   likewise for ``"of" inversions'' ("boots of speed" vs "speed boots") */
static const struct alt_spellings {
    const char *sp;
    int ob;
} spellings[] = {
    { "pickax", PICK_AXE },
    { "whip", BULLWHIP },
    { "saber", SILVER_SABER },
    { "silver sabre", SILVER_SABER },
    { "smooth shield", SHIELD_OF_REFLECTION },
    { "grey dragon scale mail", GRAY_DRAGON_SCALE_MAIL },
    { "grey dragon scales", GRAY_DRAGON_SCALES },
    { "iron ball", HEAVY_IRON_BALL },
    { "lantern", BRASS_LANTERN },
    { "mattock", DWARVISH_MATTOCK },
    { "amulet of poison resistance", AMULET_VERSUS_POISON },
    { "amulet of protection", AMULET_OF_GUARDING },
    { "amulet of telepathy", AMULET_OF_ESP },
    { "helm of esp", HELM_OF_TELEPATHY },
    { "gauntlets of ogre power", GAUNTLETS_OF_POWER },
    { "gauntlets of giant strength", GAUNTLETS_OF_POWER },
    { "elven chain mail", ELVEN_MITHRIL_COAT },
    { "potion of sleep", POT_SLEEPING },
    { "scroll of recharging", SCR_CHARGING },
    { "recharging", SCR_CHARGING },
    { "stone", ROCK },
    { "camera", EXPENSIVE_CAMERA },
    { "tee shirt", T_SHIRT },
    { "can", TIN },
    { "can opener", TIN_OPENER },
    { "kelp", KELP_FROND },
    { "eucalyptus", EUCALYPTUS_LEAF },
    { "lembas", LEMBAS_WAFER },
    { "cookie", FORTUNE_COOKIE },
    { "pie", CREAM_PIE },
    { "marker", MAGIC_MARKER },
    { "hook", GRAPPLING_HOOK },
    { "grappling iron", GRAPPLING_HOOK },
    { "grapnel", GRAPPLING_HOOK },
    { "grapple", GRAPPLING_HOOK },
    { "protection from shape shifters", RIN_PROTECTION_FROM_SHAPE_CHAN },
    /* if we ever add other sizes, move this to o_ranges[] with "bag" */
    { "box", LARGE_BOX },
    /* normally we wouldn't have to worry about unnecessary <space>, but
       " stone" will get stripped off, preventing a wishymatch; that actually
       lets "flint stone" be a match, so we also accept bogus "flintstone" */
    { "luck stone", LUCKSTONE },
    { "load stone", LOADSTONE },
    { "touch stone", TOUCHSTONE },
    { "flintstone", FLINT },
    { (const char *) 0, 0 },
};

#define BSTRCMPI(base, ptr, str) ((ptr) < base || strcmpi((ptr), str))
#define BSTRNCMPI(base, ptr, str, num) \
    ((ptr) < base || strncmpi((ptr), str, num))

/* in wizard mode, readobjnam() can accept wishes for traps and terrain */
static struct obj *
wizterrainwish(struct _readobjnam_data *d)
{
    struct rm *lev;
    boolean madeterrain = FALSE, badterrain = FALSE, didblock;
    int trap, oldtyp, x = u.ux, y = u.uy;
    char *bp = d->bp, *p = d->p;

    for (trap = NO_TRAP + 1; trap < TRAPNUM; trap++) {
        struct trap *t;
        const char *tname;

        tname = trapname(trap, TRUE);
        if (!str_start_is(bp, tname, TRUE))
            continue;
        /* found it; avoid stupid mistakes */
        if (is_hole(trap) && !Can_fall_thru(&u.uz))
            trap = ROCKTRAP;
        if ((t = maketrap(x, y, trap)) != 0) {
            trap = t->ttyp;
            tname = trapname(trap, TRUE);
            pline("%s%s.", An(tname),
                  (trap != MAGIC_PORTAL) ? "" : " to nowhere");
        } else
            pline("Creation of %s failed.", an(tname));
        return (struct obj *) &cg.zeroobj;
    }

    /* furniture and terrain (use at your own risk; can clobber stairs
       or place furniture on existing traps which shouldn't be allowed) */
    lev = &levl[x][y];
    oldtyp = lev->typ;
    didblock = does_block(x, y, lev);
    p = eos(bp);
    if (!BSTRCMPI(bp, p - 8, "fountain")) {
        lev->typ = FOUNTAIN;
        g.level.flags.nfountains++;
        lev->looted = d->looted ? F_LOOTED : 0; /* overlays 'flags' */
        lev->blessedftn = !strncmpi(bp, "magic ", 6);
        pline("A %sfountain.", lev->blessedftn ? "magic " : "");
        madeterrain = TRUE;
    } else if (!BSTRCMPI(bp, p - 6, "throne")) {
        lev->typ = THRONE;
        lev->looted = d->looted ? T_LOOTED : 0; /* overlays 'flags' */
        pline("A throne.");
        madeterrain = TRUE;
    } else if (!BSTRCMPI(bp, p - 4, "sink")) {
        lev->typ = SINK;
        g.level.flags.nsinks++;
        lev->looted = d->looted ? (S_LPUDDING | S_LDWASHER | S_LRING) : 0;
        pline("A sink.");
        madeterrain = TRUE;

    /* ("water" matches "potion of water" rather than terrain) */
    } else if (!BSTRCMPI(bp, p - 4, "pool")
               || !BSTRCMPI(bp, p - 4, "moat")
               || !BSTRCMPI(bp, p - 13, "wall of water")) {
        long save_prop;
        const char *new_water;

        lev->typ = !BSTRCMPI(bp, p - 4, "pool") ? POOL
                   : !BSTRCMPI(bp, p - 4, "moat") ? MOAT
                     : WATER;
        lev->flags = 0;
        del_engr_at(x, y);
        save_prop = EHalluc_resistance;
        EHalluc_resistance = 1;
        new_water = waterbody_name(x, y);
        EHalluc_resistance = save_prop;
        pline("%s.", An(new_water));
        /* Must manually make kelp! */
        water_damage_chain(g.level.objects[x][y], TRUE);
        madeterrain = TRUE;

    /* also matches "molten lava" */
    } else if (!BSTRCMPI(bp, p - 4, "lava")) {
        lev->typ = LAVAPOOL;
        lev->flags = 0;
        del_engr_at(x, y);
        pline("A pool of molten lava.");
        if (!(Levitation || Flying))
            pooleffects(FALSE);
        madeterrain = TRUE;
    } else if (!BSTRCMPI(bp, p - 3, "ice")) {
        lev->typ = ICE;
        lev->flags = 0;
        del_engr_at(x, y);

        if (!strncmpi(bp, "melting ", 8))
            start_melt_ice_timeout(x, y, 0L);

        pline("Ice.");
        madeterrain = TRUE;
    } else if (!BSTRCMPI(bp, p - 5, "altar")) {
        aligntyp al;

        lev->typ = ALTAR;
        if (!strncmpi(bp, "chaotic ", 8))
            al = A_CHAOTIC;
        else if (!strncmpi(bp, "neutral ", 8))
            al = A_NEUTRAL;
        else if (!strncmpi(bp, "lawful ", 7))
            al = A_LAWFUL;
        else if (!strncmpi(bp, "unaligned ", 10))
            al = A_NONE;
        else /* -1 - A_CHAOTIC, 0 - A_NEUTRAL, 1 - A_LAWFUL */
            al = !rn2(6) ? A_NONE : (rn2((int) A_LAWFUL + 2) - 1);
        lev->altarmask = Align2amask(al); /* overlays 'flags' */
        pline("%s altar.", An(align_str(al)));
        madeterrain = TRUE;
    } else if (!BSTRCMPI(bp, p - 5, "grave")
               || !BSTRCMPI(bp, p - 9, "headstone")) {
        make_grave(x, y, (char *) 0);
        if (IS_GRAVE(lev->typ)) {
            lev->looted = 0; /* overlays 'flags' */
            lev->disturbed = d->looted ? 1 : 0;
            pline("A %sgrave.", lev->disturbed ? "disturbed " : "");
            madeterrain = TRUE;
        } else {
            pline("Can't place a grave here.");
            badterrain = TRUE;
        }
    } else if (!BSTRCMPI(bp, p - 4, "tree")) {
        lev->typ = TREE;
        lev->looted = d->looted ? (TREE_LOOTED | TREE_SWARM) : 0;
        pline("A tree.");
        madeterrain = TRUE;
    } else if (!BSTRCMPI(bp, p - 4, "bars")) {
        lev->typ = IRONBARS;
        lev->flags = 0;
        /* [FIXME: if this isn't a wall or door location where 'horizontal'
            is already set up, that should be calculated for this spot.
            Unforutnately, it can be tricky; placing one in open space
            and then another adjacent might need to recalculate first one.] */
        pline("Iron bars.");
        madeterrain = TRUE;
    } else if (!BSTRCMPI(bp, p - 5, "cloud")) {
        lev->typ = CLOUD;
        lev->flags = 0;
        pline("A cloud.");
        madeterrain = TRUE;
    } else if (!BSTRCMPI(bp, p - 4, "door")
               || (d->doorless && !BSTRCMPI(bp, p - 7, "doorway"))) {
        char dbuf[40];
        unsigned old_wall_info;
        boolean secret = !BSTRCMPI(bp, p - 11, "secret door");

        /* require door or wall so that the 'horizontal' flag will
           already have the correct value; player might choose to put
           DOOR on top of existing DOOR or SDOOR on top of existing SDOOR
           to control its trapped state; iron bars are surrogate walls;
           a previously dug wall looks like corridor but is actually a
           doorless doorway so will be acceptable here */
        if (lev->typ == DOOR || lev->typ == SDOOR
            || (IS_WALL(lev->typ) && lev->typ != DBWALL)
            || lev->typ == IRONBARS) {
            /* remember previous wall info [is this right for iron bars?] */
            old_wall_info = (lev->typ != DOOR) ? lev->wall_info : 0;
            /* set the new terrain type */
            lev->typ = secret ? SDOOR : DOOR;
            lev->wall_info = 0; /* overlays 'flags' */
            /* lev->horizontal stays as-is */
            if (Is_rogue_level(&u.uz)) {
                /* all doors on the rogue level are doorless; locking magic
                   there converts them into walls rather than closed doors */
                d->doorless = 1;
                d->locked = d->closed = d->open = d->broken = 0;
            }
            /* if not locked, secret doors are implicitly closed but
               mustn't be set that way explicitly because they use both
               doormask and wall_info which both overload rm[x][y].flags
               (CLOSED overlaps wall_info bits, LOCKED and TRAPPED don't);
               conversion from SDOOR to DOOR changes NODOOR to CLOSED */
            lev->doormask = d->locked ? D_LOCKED
                            : (d->doorless || secret) ? D_NODOOR
                              : d->open ? D_ISOPEN
                                : d->broken ? D_BROKEN
                                  : D_CLOSED;
            /* SDOOR uses wall_info, restore relevant bits.
             * FIXME? if we're changing a regular door into a secret door,
             * old_wall_info bits will be 0 instead of being set properly.
             * Probably only matters if player uses Passes_walls and a wish
             * to turn a T- or cross-wall into a door, losing wall info,
             * and then another wish to turn that door into a secret door. */
            if (secret)
                lev->wall_info |= (old_wall_info & WM_MASK);
            /* set up trapped flag; open door states aren't eligible */
            if (d->trapped == 2 /* 2: wish includes explicit "untrapped" */
                || secret /* secret doors can't trapped due to their use
                           * of both doormask and wall_info; those both
                           * overlay rm->flags and partially conflict */
                || (lev->doormask & (D_LOCKED | D_CLOSED)) == 0)
                d->trapped = 0;
            if (d->trapped)
                lev->doormask |= D_TRAPPED;
            /* feedback */
            dbuf[0] = '\0';
            if (lev->doormask & D_TRAPPED)
                Strcat(dbuf, "trapped ");
            if (lev->doormask & D_LOCKED)
                Strcat(dbuf, "locked ");
            if (lev->typ == SDOOR) {
                Strcat(dbuf, "secret door");
            } else {
                /* these should be mutually exclusive but we describe them
                   as if they're independent to maybe catch future bugs... */
                if (lev->doormask & D_CLOSED)
                    Strcat(dbuf, "closed ");
                if (lev->doormask & D_ISOPEN)
                    Strcat(dbuf, "open ");
                if (lev->doormask & D_BROKEN)
                    Strcat(dbuf, "broken ");
                if ((lev->doormask & ~D_TRAPPED) == D_NODOOR)
                    Strcat(dbuf, "doorless doorway");
                else
                    Strcat(dbuf, "door");
            }
            pline("%s.", upstart(an(dbuf)));
            madeterrain = TRUE;
        } else {
            Strcpy(dbuf, secret ? "secret door" : "door");
            pline("%s requires door or wall location.", upstart(dbuf));
            badterrain = TRUE;
        }
    } else if (!BSTRCMPI(bp, p - 4, "wall")
                         && (bp == p - 4 || p[-4] == ' ')) {
        schar wall = HWALL;

        if ((isok(u.ux, u.uy-1) && IS_WALL(levl[u.ux][u.uy-1].typ))
            || (isok(u.ux, u.uy+1) && IS_WALL(levl[u.ux][u.uy+1].typ)))
            wall = VWALL;
        madeterrain = TRUE;
        lev->typ = wall;
        fix_wall_spines(max(0,u.ux-1), max(0,u.uy-1),
                        min(COLNO,u.ux+1), min(ROWNO,u.uy+1));
        pline("A wall.");
    } else if (!BSTRCMPI(bp, p - 15, "secret corridor")) {
        if (lev->typ == CORR) {
            lev->typ = SCORR;
            /* neither CORR nor SCORR uses 'flags' or 'horizontal' */
            pline("Secret corridor.");
            madeterrain = TRUE;
        } else {
            pline("Secret corridor requires corridor location.");
            badterrain = TRUE;
        }
    }

    if (madeterrain) {
        feel_newsym(x, y); /* map the spot where the wish occurred */

        /* hero started at <x,y> but might not be there anymore (create
           lava, decline to die, and get teleported away to safety) */
        if (u.uinwater && !is_pool(u.ux, u.uy)) {
            set_uinwater(0); /* u.uinwater = 0; leave the water */
            docrt();
            /* [block/unblock_point was handled by docrt -> vision_recalc] */
        } else {
            if (u.utrap && u.utraptype == TT_LAVA && !is_lava(u.ux, u.uy))
                reset_utrap(FALSE);

            if (does_block(x, y, lev)) {
                if (!didblock)
                    block_point(x, y);
            } else {
                if (didblock)
                    unblock_point(x, y);
            }
        }

        /* fixups for replaced terrain that aren't handled above;
           for fountain placed on fountain or sink placed on sink, the
           increment above gets canceled out by the decrement here;
           otherwise if fountain or sink was replaced, there's one less */
        if (IS_FOUNTAIN(oldtyp))
            g.level.flags.nfountains--;
        else if (IS_SINK(oldtyp))
            g.level.flags.nsinks--;
        /* horizontal is overlaid by fountain->blessedftn, grave->disturbed */
        if (IS_FOUNTAIN(oldtyp) || IS_GRAVE(oldtyp)
            || IS_WALL(oldtyp) || oldtyp == IRONBARS
            || IS_DOOR(oldtyp) || oldtyp == SDOOR) {
            /* when new terrain is a fountain, 'blessedftn' was explicitly
               set above; likewise for grave and 'disturbed'; when it's a
               door, the old type was a wall or a door and we retain the
               'horizontal' value from those */
            if (!IS_FOUNTAIN(lev->typ) && !IS_GRAVE(lev->typ)
                && !IS_DOOR(lev->typ) && lev->typ != SDOOR)
                lev->horizontal = 0; /* also clears blessedftn, disturbed */
        }
        /* note: lev->lit and lev->nondiggable retain their values even
           though those might not make sense with the new terrain */

        /* might have changed terrain from something that blocked
           levitation and flying to something that doesn't (levitating
           while in xorn form and replacing solid stone with furniture) */
        switch_terrain();
    }
    if (madeterrain || badterrain) {
        /* cast 'const' away; caller won't modify this */
        return (struct obj *) &cg.zeroobj;
    }

    return (struct obj *) 0;
}

#define UNDEFINED 0
#define EMPTY 1
#define SPINACH 2

static void
readobjnam_init(char *bp, struct _readobjnam_data *d)
{
    d->otmp = (struct obj *) 0;
    d->cnt = d->spe = d->spesgn = d->typ = 0;
    d->very = d->rechrg = d->blessed = d->uncursed = d->iscursed
        = d->ispoisoned = d->isgreased = d->eroded = d->eroded2
        = d->erodeproof = d->halfeaten = d->islit = d->unlabeled
        = d->ishistoric = d->isdiluted /* statues, potions */
          /* box/chest and wizard mode door */
        = d->trapped = d->locked = d->unlocked = d->broken
        = d->open = d->closed = d->doorless /* wizard mode door */
        = d->looted /* wizard mode fountain/sink/throne/tree and grave */
        = d->real = d->fake = 0; /* Amulet */
    d->tvariety = RANDOM_TIN;
    d->mgend = -1; /* not specified, aka random */
    d->mntmp = NON_PM;
    d->contents = UNDEFINED;
    d->oclass = 0;
    d->actualn = d->dn = d->un = 0;
    d->wetness = 0;
    d->gsize = 0;
    d->zombify = FALSE;
    d->bp = d->origbp = bp;
    d->p = (char *) 0;
    d->name = (const char *) 0;
    d->ftype = g.context.current_fruit;
    (void) memset(d->globbuf, '\0', sizeof d->globbuf);
    (void) memset(d->fruitbuf, '\0', sizeof d->fruitbuf);
}

/* return 1 if d->bp is empty or contains only various qualifiers like
   "blessed", "rustproof", and so on, or 0 if anything else is present */
static int
readobjnam_preparse(struct _readobjnam_data *d)
{
    char *save_bp = 0;
    int more_l = 0, res = 1;

    for (;;) {
        register int l;

        if (!d->bp || !*d->bp)
            break;
        res = 0;

        if (!strncmpi(d->bp, "an ", l = 3) || !strncmpi(d->bp, "a ", l = 2)) {
            d->cnt = 1;
        } else if (!strncmpi(d->bp, "the ", l = 4)) {
            ; /* just increment `bp' by `l' below */
        } else if (!d->cnt && digit(*d->bp) && strcmp(d->bp, "0")) {
            d->cnt = atoi(d->bp);
            while (digit(*d->bp))
                d->bp++;
            while (*d->bp == ' ')
                d->bp++;
            l = 0;
        } else if (*d->bp == '+' || *d->bp == '-') {
            d->spesgn = (*d->bp++ == '+') ? 1 : -1;
            d->spe = atoi(d->bp);
            while (digit(*d->bp))
                d->bp++;
            while (*d->bp == ' ')
                d->bp++;
            l = 0;
        } else if (!strncmpi(d->bp, "blessed ", l = 8)
                   || !strncmpi(d->bp, "holy ", l = 5)) {
            d->blessed = 1, d->uncursed = d->iscursed = 0;
        } else if (!strncmpi(d->bp, "cursed ", l = 7)
                   || !strncmpi(d->bp, "unholy ", l = 7)) {
            d->iscursed = 1, d->blessed = d->uncursed = 0;
        } else if (!strncmpi(d->bp, "uncursed ", l = 9)) {
            d->uncursed = 1, d->blessed = d->iscursed = 0;
        } else if (!strncmpi(d->bp, "rustproof ", l = 10)
                   || !strncmpi(d->bp, "erodeproof ", l = 11)
                   || !strncmpi(d->bp, "corrodeproof ", l = 13)
                   || !strncmpi(d->bp, "fixed ", l = 6)
                   || !strncmpi(d->bp, "fireproof ", l = 10)
                   || !strncmpi(d->bp, "rotproof ", l = 9)) {
            d->erodeproof = 1;
        } else if (!strncmpi(d->bp, "lit ", l = 4)
                   || !strncmpi(d->bp, "burning ", l = 8)) {
            d->islit = 1;
        } else if (!strncmpi(d->bp, "unlit ", l = 6)
                   || !strncmpi(d->bp, "extinguished ", l = 13)) {
            d->islit = 0;

        /* "wet" and "moist" are only applicable for towels */
        } else if (!strncmpi(d->bp, "moist ", l = 6)
                   || !strncmpi(d->bp, "wet ", l = 4)) {
            if (!strncmpi(d->bp, "wet ", 4))
                d->wetness = 3 + rn2(3); /* 3..5 */
            else
                d->wetness = rnd(2); /* 1..2 */

        /* "unlabeled" and "blank" are synonymous */
        } else if (!strncmpi(d->bp, "unlabeled ", l = 10)
                   || !strncmpi(d->bp, "unlabelled ", l = 11)
                   || !strncmpi(d->bp, "blank ", l = 6)) {
            d->unlabeled = 1;
        } else if (!strncmpi(d->bp, "poisoned ", l = 9)) {
            d->ispoisoned = 1;

        /* "trapped" recognized but not honored outside wizard mode */
        } else if (!strncmpi(d->bp, "trapped ", l = 8)) {
            d->trapped = 0; /* undo any previous "untrapped" */
            if (wizard)
                d->trapped = 1;
        } else if (!strncmpi(d->bp, "untrapped ", l = 10)) {
            d->trapped = 2; /* not trapped */

        /* locked, unlocked, broken: box/chest lock states, also door states;
           open, closed, doorless: additional door states */
        } else if (!strncmpi(d->bp, "locked ", l = 7)) {
            d->locked = d->closed = 1,
                d->unlocked = d->broken = d->open = d->doorless = 0;
        } else if (!strncmpi(d->bp, "unlocked ", l = 9)) {
            d->unlocked = d->closed = 1,
                d->locked = d->broken = d->open = d->doorless = 0;
        } else if (!strncmpi(d->bp, "broken ", l = 7)) {
            d->broken = 1,
                d->locked = d->unlocked = d->open = d->closed
                = d->doorless = 0;
        } else if (!strncmpi(d->bp, "open ", l = 5)) {
            d->open = 1,
                d->closed = d->locked = d->broken = d->doorless = 0;
        } else if (!strncmpi(d->bp, "closed ", l = 7)) {
            d->closed = 1,
                d->open = d->locked = d->broken = d->doorless = 0;
        } else if (!strncmpi(d->bp, "doorless ", l = 9)) {
            d->doorless = 1,
                d->open = d->closed = d->locked = d->unlocked = d->broken = 0;
        /* looted: fountain/sink/throne/tree; disturbed: grave */
        } else if (!strncmpi(d->bp, "looted ", l = 7)
                   /* overload disturbed grave with looted fountain here
                      even though they're separate in struct rm */
                   || !strncmpi(d->bp, "disturbed ", l = 10)) {
            d->looted = 1;
        } else if (!strncmpi(d->bp, "greased ", l = 8)) {
            d->isgreased = 1;
        } else if (!strncmpi(d->bp, "zombifying ", l = 11)) {
            d->zombify = TRUE;
        } else if (!strncmpi(d->bp, "very ", l = 5)) {
            /* very rusted very heavy iron ball */
            d->very = 1;
        } else if (!strncmpi(d->bp, "thoroughly ", l = 11)) {
            d->very = 2;
        } else if (!strncmpi(d->bp, "rusty ", l = 6)
                   || !strncmpi(d->bp, "rusted ", l = 7)
                   || !strncmpi(d->bp, "burnt ", l = 6)
                   || !strncmpi(d->bp, "burned ", l = 7)) {
            d->eroded = 1 + d->very;
            d->very = 0;
        } else if (!strncmpi(d->bp, "corroded ", l = 9)
                   || !strncmpi(d->bp, "rotted ", l = 7)) {
            d->eroded2 = 1 + d->very;
            d->very = 0;
        } else if (!strncmpi(d->bp, "partly eaten ", l = 13)
                   || !strncmpi(d->bp, "partially eaten ", l = 16)) {
            d->halfeaten = 1;
        } else if (!strncmpi(d->bp, "historic ", l = 9)) {
            d->ishistoric = 1;
        } else if (!strncmpi(d->bp, "diluted ", l = 8)) {
            d->isdiluted = 1;
        } else if (!strncmpi(d->bp, "empty ", l = 6)) {
            d->contents = EMPTY;
        } else if (!strncmpi(d->bp, "small ", l = 6)) { /* glob sizes */
            /* "small" might be part of monster name (mimic, if wishing
               for its corpse) rather than prefix for glob size; when
               used for globs, it might be either "small glob of <foo>" or
               "small <foo> glob" and user might add 's' even though plural
               doesn't accomplish anything because globs don't stack */
            if (strncmpi(d->bp + l, "glob", 4) && !strstri(d->bp + l, " glob"))
                break;
            d->gsize = 1;
        } else if (!strncmpi(d->bp, "medium ", l = 7)) {
            /* 3.7: in 3.6, "medium" was only used during wishing and the
               mid-size glob had no adjective when formatted, but as of
               3.7, "medium" has become an explicit part of the name for
               combined globs of at least 5 individual ones (owt >= 100)
               and less than 15 (owt < 300) */
            d->gsize = 2;
        } else if (!strncmpi(d->bp, "large ", l = 6)) {
            /* "large" might be part of monster name (dog, cat, koboold,
               mimic) or object name (box, round shield) rather than
               prefix for glob size */
            if (strncmpi(d->bp + l, "glob", 4) && !strstri(d->bp + l, " glob"))
                break;
            /* "very large " had "very " peeled off on previous iteration */
            d->gsize = (d->very != 1) ? 3 : 4;
        } else if (!strncmpi(d->bp, "real ", l = 5)) {
            /* accept "real Amulet of Yendor" with "blessed" or "cursed"
               or useless "erodeproof" before or after "real" ... */
            d->real = 1; /* don't negate 'fake' here; "real fake amulet" and
                       * "fake real amulet" will both yield fake amulet
                       * (so will "real amulet" outside of wizard mode) */
        } else if (!strncmpi(d->bp, "fake ", l = 5)) {
            /* ... and "fake Amulet of Yendor" likewise */
            d->fake = 1, d->real = 0;
            /* ['real' isn't actually needed (unless we someday add
               "real gem" for random non-glass, non-stone)] */
        } else if (!strncmpi(d->bp, "female ", l = 7)) {
            d->mgend = FEMALE;
            /* if after "corpse/statue/figurine of", remove from string */
            if (save_bp)
                strsubst(d->bp, "female ", ""), l = 0;
        } else if (!strncmpi(d->bp, "male ", l = 5)) {
            d->mgend = MALE;
            if (save_bp)
                strsubst(d->bp, "male ", ""), l = 0;
        } else if (!strncmpi(d->bp, "neuter ", l = 7)) {
            d->mgend = NEUTRAL;
            if (save_bp)
                strsubst(d->bp, "neuter ", ""), l = 0;

        /*
         * Corpse/statue/figurine gender hack:  in order to accept
         * "statue of a female gnome ruler" for gnome queen we need
         * to recognize and skip over "statue of [a ]".  Otherwise
         * we would only accept "female gnome ruler statue" and the
         * viable but silly "female statue of a gnome ruler".
         */
        } else if ((!strncmpi(d->bp, "corpse ", l = 7)
                    || !strncmpi(d->bp, "statue ", l = 7)
                    || !strncmpi(d->bp, "figurine ", l = 9))
                   && !strncmpi(d->bp + l, "of ", more_l = 3)) {
            save_bp = d->bp; /* we'll backtrack to here later */
            l += more_l, more_l = 0;
            if (!strncmpi(d->bp + l, "a ", more_l = 2)
                || !strncmpi(d->bp + l, "an ", more_l = 3)
                || !strncmpi(d->bp + l, "the ", more_l = 4))
                l += more_l;
        } else {
            break;
        }
        d->bp += l;
    }
    if (save_bp)
        d->bp = save_bp;
    return res;
}

static void
readobjnam_parse_charges(struct _readobjnam_data *d)
{
    if (strlen(d->bp) > 1 && (d->p = rindex(d->bp, '(')) != 0) {
        boolean keeptrailingchars = TRUE;
        int idx = 0;

        if (d->p > d->bp && d->p[-1] == ' ')
            idx = -1;
        d->p[idx] = '\0'; /* terminate bp */
        ++d->p; /* advance past '(' */
        if (!strncmpi(d->p, "lit)", 4)) {
            d->islit = 1;
            d->p += 4 - 1; /* point at ')' */
        } else {
            d->spe = atoi(d->p);
            while (digit(*d->p))
                d->p++;
            if (*d->p == ':') {
                d->p++;
                d->rechrg = d->spe;
                d->spe = atoi(d->p);
                while (digit(*d->p))
                    d->p++;
            }
            if (*d->p != ')') {
                d->spe = d->rechrg = 0;
                /* mis-matched parentheses; rest of string will be ignored
                 * [probably we should restore everything back to '('
                 * instead since it might be part of "named ..."]
                 */
                keeptrailingchars = FALSE;
            } else {
                d->spesgn = 1;
            }
        }
        if (keeptrailingchars) {
            char *pp = eos(d->bp);

            /* 'pp' points at 'pb's terminating '\0',
               'p' points at ')' and will be incremented past it */
            do {
                *pp++ = *++d->p;
            } while (*d->p);
        }
    }
    /*
     * otmp->spe is type schar, so we don't want spe to be any bigger or
     * smaller.  Also, spe should always be positive --some cheaters may
     * try to confuse atoi().
     */
    if (d->spe < 0) {
        d->spesgn = -1; /* cheaters get what they deserve */
        d->spe = abs(d->spe);
    }
    /* cap on obj->spe is independent of (and less than) SCHAR_LIM */
    if (d->spe > SPE_LIM)
        d->spe = SPE_LIM; /* slime mold uses d.ftype, so not affected */
    if (d->rechrg < 0 || d->rechrg > 7)
        d->rechrg = 7; /* recharge_limit */
}

static const char *const wrp[] = {
    "wand",   "ring",      "potion",     "scroll", "gem",
    "amulet", "spellbook", "spell book",
    /* for non-specific wishes */
    "weapon", "armor",     "tool",       "food",   "comestible",
};
static const char wrpsym[] = { WAND_CLASS,   RING_CLASS,   POTION_CLASS,
                               SCROLL_CLASS, GEM_CLASS,    AMULET_CLASS,
                               SPBOOK_CLASS, SPBOOK_CLASS, WEAPON_CLASS,
                               ARMOR_CLASS,  TOOL_CLASS,   FOOD_CLASS,
                               FOOD_CLASS };


static int
readobjnam_postparse1(struct _readobjnam_data *d)
{
    int i;

    /* now we have the actual name, as delivered by xname, say
     *  green potions called whisky
     *  scrolls labeled "QWERTY"
     *  egg
     *  fortune cookies
     *  very heavy iron ball named hoei
     *  wand of wishing
     *  elven cloak
     */
    if ((d->p = strstri(d->bp, " named ")) != 0) {
        *d->p = 0;
        d->name = d->p + 7;
    }
    if ((d->p = strstri(d->bp, " called ")) != 0) {
        *d->p = 0;
        d->un = d->p + 8;
        /* "helmet called telepathy" is not "helmet" (a specific type)
         * "shield called reflection" is not "shield" (a general type)
         */
        for (i = 0; i < SIZE(o_ranges); i++)
            if (!strcmpi(d->bp, o_ranges[i].name)) {
                d->oclass = o_ranges[i].oclass;
                return 1; /*goto srch;*/
            }
    }
    if ((d->p = strstri(d->bp, " labeled ")) != 0) {
        *d->p = 0;
        d->dn = d->p + 9;
    } else if ((d->p = strstri(d->bp, " labelled ")) != 0) {
        *d->p = 0;
        d->dn = d->p + 10;
    }
    if ((d->p = strstri(d->bp, " of spinach")) != 0) {
        *d->p = 0;
        d->contents = SPINACH;
    }
    /* real vs fake is only useful for wizard mode but we'll accept its
       parsing in normal play (result is never real Amulet for that case) */
    if ((d->p = strstri(d->bp, OBJ_DESCR(objects[AMULET_OF_YENDOR]))) != 0
        && (d->p == d->bp || d->p[-1] == ' ')) {
        char *s = d->bp;

        /* "Amulet of Yendor" matches two items, name of real Amulet
           and description of fake one; player can explicitly specify
           "real" to disambiguate, but not specifying "fake" achieves
           the same thing; "real" and "fake" are parsed above with other
           prefixes so that combinations like "blessed real" and "real
           blessed" work as expected; also accept partial specification
           of the full name of the fake; unlike the prefix recognition
           loop above, these have to be in the right order when more
           than one is present (similar to worthless glass gems below) */
        if (!strncmpi(s, "cheap ", 6))
            d->fake = 1, s += 6;
        if (!strncmpi(s, "plastic ", 8))
            d->fake = 1, s += 8;
        if (!strncmpi(s, "imitation ", 10))
            d->fake = 1, s += 10;
        nhUse(s); /* suppress potential assigned-but-not-used complaint */
        /* when 'fake' is True, it overrides 'real' if both were given;
           when it is False, force 'real' whether that was specified or not */
        d->real = !d->fake;
        d->typ = d->real ? AMULET_OF_YENDOR : FAKE_AMULET_OF_YENDOR;
        return 2; /*goto typfnd;*/
    }

    /*
     * Skip over "pair of ", "pairs of", "set of" and "sets of".
     *
     * Accept "3 pair of boots" as well as "3 pairs of boots".  It is
     * valid English either way.  See makeplural() for more on pair/pairs.
     *
     * We should only double count if the object in question is not
     * referred to as a "pair of".  E.g. We should double if the player
     * types "pair of spears", but not if the player types "pair of
     * lenses".  Luckily (?) all objects that are referred to as pairs
     * -- boots, gloves, and lenses -- are also not mergable, so cnt is
     * ignored anyway.
     */
    if (!strncmpi(d->bp, "pair of ", 8)) {
        d->bp += 8;
        d->cnt *= 2;
    } else if (!strncmpi(d->bp, "pairs of ", 9)) {
        d->bp += 9;
        if (d->cnt > 1)
            d->cnt *= 2;
    } else if (!strncmpi(d->bp, "set of ", 7)) {
        d->bp += 7;
    } else if (!strncmpi(d->bp, "sets of ", 8)) {
        d->bp += 8;
    }

    /* Intercept pudding globs here; they're a valid wish target,
     * but we need them to not get treated like a corpse.
     * If a count is specified, it will be used to magnify weight
     * rather than to specify quantity (which is always 1 for globs).
     */
    i = (int) strlen(d->bp);
    d->p = (char *) 0;
    /* check for "glob", "<foo> glob", and "glob of <foo>" */
    if (!strcmpi(d->bp, "glob") || !BSTRCMPI(d->bp, d->bp + i - 5, " glob")
        || !strcmpi(d->bp, "globs")
        || !BSTRCMPI(d->bp, d->bp + i - 6, " globs")
        || (d->p = strstri(d->bp, "glob of ")) != 0
        || (d->p = strstri(d->bp, "globs of ")) != 0) {
        d->mntmp = name_to_mon(!d->p ? d->bp
                                     : (strstri(d->p, " of ") + 4), (int *) 0);
        /* if we didn't recognize monster type, pick a valid one at random */
        if (d->mntmp == NON_PM)
            d->mntmp = rn1(PM_BLACK_PUDDING - PM_GRAY_OOZE, PM_GRAY_OOZE);
        /* normally this would be done when makesingular() changes the value
           but canonical form here is already singular so that won't happen */
        if (d->cnt < 2 && strstri(d->bp, "globs"))
            d->cnt = 2; /* affects otmp->owt but not otmp->quan for globs */
        /* construct canonical spelling in case name_to_mon() recognized a
           variant (grey ooze) or player used inverted syntax (<foo> glob);
           if player has given a valid monster type but not valid glob type,
           object name lookup won't find it and wish attempt will fail */
        Sprintf(d->globbuf, "glob of %s", mons[d->mntmp].pmnames[NEUTRAL]);
        d->bp = d->globbuf;
        d->mntmp = NON_PM; /* not useful for "glob of <foo>" object lookup */
        d->oclass = FOOD_CLASS;
        d->actualn = d->bp, d->dn = 0;
        return 1; /*goto srch;*/
    } else {
        /*
         * Find corpse type using "of" (figurine of an orc, tin of orc meat)
         * Don't check if it's a wand or spellbook.
         * (avoid "wand/finger of death" confusion).
         * Don't match "ogre" or "giant" monster name inside alternate item
         * names "gauntlets of ogre power" and "gauntlets of giant strength"
         * (or the alternate spelling of those, "gloves of ...").
         */
        if (!strstri(d->bp, "wand ") && !strstri(d->bp, "spellbook ")
            && !strstri(d->bp, "gauntlets ") && !strstri(d->bp, "gloves ")
            && !strstri(d->bp, "finger ")) {
            if ((d->p = strstri(d->bp, "tin of ")) != 0) {
                if (!strcmpi(d->p + 7, "spinach")) {
                    d->contents = SPINACH;
                    d->mntmp = NON_PM;
                } else {
                    d->tmp = tin_variety_txt(d->p + 7, &d->tinv);
                    d->tvariety = d->tinv;
                    d->mntmp = name_to_mon(d->p + 7 + d->tmp, &d->mgend);
                }
                d->typ = TIN;
                return 2; /*goto typfnd;*/
            } else if ((d->p = strstri(d->bp, " of ")) != 0
                       && ((d->mntmp = name_to_mon(d->p + 4, &d->mgend))
                           >= LOW_PM))
                *d->p = 0;
        }
    }
    /* Find corpse type w/o "of" (red dragon scale mail, yeti corpse) */
    if (strncmpi(d->bp, "samurai sword", 13)  /* not the "samurai" monster! */
        && strncmpi(d->bp, "wizard lock", 11) /* not the "wizard" monster! */
        && strncmpi(d->bp, "death wand", 10)  /* 'of inversion', not Rider */
        && strncmpi(d->bp, "master key", 10)  /* not the "master" rank */
        && strncmpi(d->bp, "ninja-to", 8)     /* not the "ninja" rank */
        && strncmpi(d->bp, "magenta", 7)) {   /* not the "mage" rank */
        const char *rest = 0;

        if (d->mntmp < LOW_PM && strlen(d->bp) > 2
            && ((d->mntmp = name_to_monplus(d->bp, &rest, &d->mgend))
                >= LOW_PM)) {
            char *obp = d->bp;

            /* 'rest' is a pointer past the matching portion; if that was
               an alternate name or a rank title rather than the canonical
               monster name we wouldn't otherwise know how much to skip */
            d->bp = (char *) rest; /* cast away const */

            if (*d->bp == ' ') {
                d->bp++;
            } else if (!strncmpi(d->bp, "s ", 2)
                       || (d->bp > d->origbp
                           && !strncmpi(d->bp - 1, "s' ", 3))) {
                d->bp += 2;
            } else if (!strncmpi(d->bp, "es ", 3)
                       || !strncmpi(d->bp, "'s ", 3)) {
                d->bp += 3;
            } else if (!*d->bp && !d->actualn && !d->dn && !d->un
                       && !d->oclass) {
                /* no referent; they don't really mean a monster type */
                d->bp = obp;
                d->mntmp = NON_PM;
            }
        }
    }

    /* first change to singular if necessary */
    if (*d->bp
        /* we want "tricks" to match "bag of tricks" [rnd_otyp_by_namedesc()]
           but that wouldn't work if it gets singularized to "trick"
           ["tricks bag" matches whether or not this exception is present
           because singularize operates on "bag" and wishymatch()'s
           'of inversion' finds a match] */
        && strcmpi(d->bp, "tricks")
        /* an odd potential wish; fail rather than get a false match with
           "cloth" because it might yield a "cloth spellbook" rather than
           a "piece of cloth" cloak [maybe we should give random armor?] */
        && strcmpi(d->bp, "clothes")
        ) {
        char *sng = makesingular(d->bp);

        if (strcmp(d->bp, sng)) {
            if (d->cnt == 1)
                d->cnt = 2;
            Strcpy(d->bp, sng);
        }
    }

    /* Alternate spellings (pick-ax, silver sabre, &c) */
    {
        const struct alt_spellings *as = spellings;

        while (as->sp) {
            if (wishymatch(d->bp, as->sp, TRUE)) {
                d->typ = as->ob;
                return 2; /*goto typfnd;*/
            }
            as++;
        }
        /* can't use spellings list for this one due to shuffling */
        if (!strncmpi(d->bp, "grey spell", 10))
            *(d->bp + 2) = 'a';

        if ((d->p = strstri(d->bp, "armour")) != 0) {
            /* skip past "armo", then copy remainder beyond "u" */
            d->p += 4;
            while ((*d->p = *(d->p + 1)) != '\0')
                ++d->p; /* self terminating */
        }
    }

    /* dragon scales - assumes order of dragons */
    if (!strcmpi(d->bp, "scales") && d->mntmp >= PM_GRAY_DRAGON
        && d->mntmp <= PM_YELLOW_DRAGON) {
        d->typ = GRAY_DRAGON_SCALES + d->mntmp - PM_GRAY_DRAGON;
        d->mntmp = NON_PM; /* no monster */
        return 2; /*goto typfnd;*/
    }

    d->p = eos(d->bp);
    if (!BSTRCMPI(d->bp, d->p - 10, "holy water")) {
        /* this isn't needed for "[un]holy water" because adjective parsing
           handles holy==blessed and unholy==cursed and leaves "water" for
           the object type, but it is needed for "potion of [un]holy water"
           since that parsing stops when it reaches "potion"; also, neither
           "holy water" nor "unholy water" is an actual type of potion */
        if (!BSTRNCMPI(d->bp, d->p - 10 - 2, "un", 2))
            d->iscursed = 1, d->blessed = d->uncursed = 0; /* unholy water */
        else
            d->blessed = 1, d->iscursed = d->uncursed = 0; /* holy water */
        d->typ = POT_WATER;
        return 2; /*goto typfnd;*/
    }
    /* accept "paperback" or "paperback book", reject "paperback spellbook" */
    if (!strncmpi(d->bp, "paperback", 9)) {
        char *dbp = d->bp + 9; /* just past "paperback" */

        if (!*dbp || !strncmpi(dbp, " book", 5)) {
            d->typ = SPE_NOVEL;
            return 2; /*goto typfnd;*/
        } else {
            d->otmp = (struct obj *) 0;
            return 3;
        }
    }
    if (d->unlabeled && !BSTRCMPI(d->bp, d->p - 6, "scroll")) {
        d->typ = SCR_BLANK_PAPER;
        return 2; /*goto typfnd;*/
    }
    if (d->unlabeled && !BSTRCMPI(d->bp, d->p - 9, "spellbook")) {
        d->typ = SPE_BLANK_PAPER;
        return 2; /*goto typfnd;*/
    }
    /* specific food rather than color of gem/potion/spellbook[/scales] */
    if (!BSTRCMPI(d->bp, d->p - 6, "orange") && d->mntmp == NON_PM) {
        d->typ = ORANGE;
        return 2; /*goto typfnd;*/
    }
    /*
     * NOTE: Gold pieces are handled as objects nowadays, and therefore
     * this section should probably be reconsidered as well as the entire
     * gold/money concept.  Maybe we want to add other monetary units as
     * well in the future. (TH)
     */
    if (!BSTRCMPI(d->bp, d->p - 10, "gold piece")
        || !BSTRCMPI(d->bp, d->p - 7, "zorkmid")
        || !strcmpi(d->bp, "gold") || !strcmpi(d->bp, "money")
        || !strcmpi(d->bp, "coin") || *d->bp == GOLD_SYM) {
        if (d->cnt > 5000 && !wizard)
            d->cnt = 5000;
        else if (d->cnt < 1)
            d->cnt = 1;
        d->otmp = mksobj(GOLD_PIECE, FALSE, FALSE);
        d->otmp->quan = (long) d->cnt;
        d->otmp->owt = weight(d->otmp);
        g.context.botl = 1;
        return 3; /*return otmp;*/
    }

    /* check for single character object class code ("/" for wand, &c) */
    if (strlen(d->bp) == 1 && (i = def_char_to_objclass(*d->bp)) < MAXOCLASSES
        && i > ILLOBJ_CLASS && (i != VENOM_CLASS || wizard)) {
        d->oclass = i;
        return 4; /*goto any;*/
    }

    /* Search for class names: XXXXX potion, scroll of XXXXX.  Avoid */
    /* false hits on, e.g., rings for "ring mail". */
    if (strncmpi(d->bp, "enchant ", 8)
        && strncmpi(d->bp, "destroy ", 8)
        && strncmpi(d->bp, "detect food", 11)
        && strncmpi(d->bp, "food detection", 14)
        && strncmpi(d->bp, "ring mail", 9)
        && strncmpi(d->bp, "studded leather armor", 21)
        && strncmpi(d->bp, "leather armor", 13)
        && strncmpi(d->bp, "tooled horn", 11)
        && strncmpi(d->bp, "food ration", 11)
        && strncmpi(d->bp, "meat ring", 9))
        for (i = 0; i < (int) (sizeof wrpsym); i++) {
            register int j = Strlen(wrp[i]);

            /* check for "<class> [ of ] something" */
            if (!strncmpi(d->bp, wrp[i], j)) {
                d->oclass = wrpsym[i];
                if (d->oclass != AMULET_CLASS) {
                    d->bp += j;
                    if (!strncmpi(d->bp, " of ", 4))
                        d->actualn = d->bp + 4;
                    /* else if(*bp) ?? */
                } else
                    d->actualn = d->bp;
                return 1; /*goto srch;*/
            }
            /* check for "something <class>" */
            if (!BSTRCMPI(d->bp, d->p - j, wrp[i])) {
                d->oclass = wrpsym[i];
                /* for "foo amulet", leave the class name so that
                   wishymatch() can do "of inversion" to try matching
                   "amulet of foo"; other classes don't include their
                   class name in their full object names (where
                   "potion of healing" is just "healing", for instance) */
                if (d->oclass != AMULET_CLASS) {
                    d->p -= j;
                    *d->p = '\0';
                    if (d->p > d->bp && d->p[-1] == ' ')
                        d->p[-1] = '\0';
                } else {
                    /* amulet without "of"; convoluted wording but better a
                       special case that's handled than one that's missing */
                    if (!strncmpi(d->bp, "versus poison ", 14)) {
                        d->typ = AMULET_VERSUS_POISON;
                        return 2; /*goto typfnd;*/
                    }
                }
                d->actualn = d->dn = d->bp;
                return 1; /*goto srch;*/
            }
        }

    /* Wishing in wizard mode can create traps and furniture.
     * Part I:  distinguish between trap and object for the two
     * types of traps which have corresponding objects:  bear trap
     * and land mine.  "beartrap" (object) and "bear trap" (trap)
     * have a difference in spelling which we used to exploit by
     * adding a special case in wishymatch(), but "land mine" is
     * spelled the same either way so needs different handing.
     * Since we need something else for land mine, we've dropped
     * the bear trap hack so that both are handled exactly the
     * same.  To get an armed trap instead of a disarmed object,
     * the player can prefix either the object name or the trap
     * name with "trapped " (which ordinarily applies to chests
     * and tins), or append something--anything at all except for
     * " object", but " trap" is suggested--to either the trap
     * name or the object name.
     */
    if (wizard && (!strncmpi(d->bp, "bear", 4)
                   || !strncmpi(d->bp, "land", 4))) {
        boolean beartrap = (lowc(*d->bp) == 'b');
        char *zp = d->bp + 4; /* skip "bear"/"land" */

        if (*zp == ' ')
            ++zp; /* embedded space is optional */
        if (!strncmpi(zp, beartrap ? "trap" : "mine", 4)) {
            zp += 4;
            if (d->trapped == 2 || !strcmpi(zp, " object")) {
                /* "untrapped <foo>" or "<foo> object" */
                d->typ = beartrap ? BEARTRAP : LAND_MINE;
                return 2; /*goto typfnd;*/
            } else if (d->trapped == 1 || *zp != '\0') {
                /* "trapped <foo>" or "<foo> trap" (actually "<foo>*") */
                /* use canonical trap spelling, skip object matching */
                Strcpy(d->bp, trapname(beartrap ? BEAR_TRAP : LANDMINE, TRUE));
                return 5; /*goto wiztrap;*/
            }
            /* [no prefix or suffix; we're going to end up matching
               the object name and getting a disarmed trap object] */
        }
    }

    return 0;
}

static int
readobjnam_postparse2(struct _readobjnam_data *d)
{
    int i;

    /* "grey stone" check must be before general "stone" */
    for (i = 0; i < SIZE(o_ranges); i++)
        if (!strcmpi(d->bp, o_ranges[i].name)) {
            d->typ = rnd_class(o_ranges[i].f_o_range, o_ranges[i].l_o_range);
            return 2; /*goto typfnd;*/
        }

    if (!BSTRCMPI(d->bp, d->p - 6, " stone")
        || !BSTRCMPI(d->bp, d->p - 4, " gem")) {
        d->p[!strcmpi(d->p - 4, " gem") ? -4 : -6] = '\0';
        d->oclass = GEM_CLASS;
        d->dn = d->actualn = d->bp;
        return 1; /*goto srch;*/
    } else if (!strcmpi(d->bp, "looking glass")) {
        ; /* avoid false hit on "* glass" */
    } else if (!BSTRCMPI(d->bp, d->p - 6, " glass")
               || !strcmpi(d->bp, "glass")) {
        register char *s = d->bp;

        /* treat "broken glass" as a non-existent item; since "broken" is
           also a chest/box prefix it might have been stripped off above */
        if (d->broken || strstri(s, "broken")) {
            d->otmp = (struct obj *) 0;
            return 3; /* return otmp */
        }
        if (!strncmpi(s, "worthless ", 10))
            s += 10;
        if (!strncmpi(s, "piece of ", 9))
            s += 9;
        if (!strncmpi(s, "colored ", 8))
            s += 8;
        else if (!strncmpi(s, "coloured ", 9))
            s += 9;
        if (!strcmpi(s, "glass")) { /* choose random color */
            /* 9 different kinds */
            d->typ = LAST_GEM + rnd(NUM_GLASS_GEMS);
            if (objects[d->typ].oc_class == GEM_CLASS)
                return 2; /*goto typfnd;*/
            else
                d->typ = 0; /* somebody changed objects[]? punt */
        } else { /* try to construct canonical form */
            char tbuf[BUFSZ];

            Strcpy(tbuf, "worthless piece of ");
            Strcat(tbuf, s); /* assume it starts with the color */
            Strcpy(d->bp, tbuf);
        }
    }

    d->actualn = d->bp;
    if (!d->dn)
        d->dn = d->actualn; /* ex. "skull cap" */

    return 0;
}

static int
readobjnam_postparse3(struct _readobjnam_data *d)
{
    int i;

    /* check real names of gems first */
    if (!d->oclass && d->actualn) {
        for (i = g.bases[GEM_CLASS]; i <= LAST_GEM; i++) {
            register const char *zn;

            if ((zn = OBJ_NAME(objects[i])) != 0 && !strcmpi(d->actualn, zn)) {
                d->typ = i;
                return 2; /*goto typfnd;*/
            }
        }
        /* "tin of foo" would be caught above, but plain "tin" has
           a random chance of yielding "tin wand" unless we do this */
        if (!strcmpi(d->actualn, "tin")) {
            d->typ = TIN;
            return 2; /*goto typfnd;*/
        }
    }

    if (((d->typ = rnd_otyp_by_namedesc(d->actualn, d->oclass, 1))
         != STRANGE_OBJECT)
        || (d->dn != d->actualn
            && ((d->typ = rnd_otyp_by_namedesc(d->dn, d->oclass, 1))
                != STRANGE_OBJECT))
        || ((d->typ = rnd_otyp_by_namedesc(d->un, d->oclass, 1))
             != STRANGE_OBJECT)
        || (d->origbp != d->actualn
            && ((d->typ = rnd_otyp_by_namedesc(d->origbp, d->oclass, 1))
                != STRANGE_OBJECT)))
        return 2; /*goto typfnd;*/
    d->typ = 0;

    if (d->actualn) {
        struct Jitem *j = Japanese_items;

        while (j->item) {
            if (d->actualn && !strcmpi(d->actualn, j->name)) {
                d->typ = j->item;
                return 2; /*goto typfnd;*/
            }
            j++;
        }
    }
    /* if we've stripped off "armor" and failed to match anything
       in objects[], append "mail" and try again to catch misnamed
       requests like "plate armor" and "yellow dragon scale armor" */
    if (d->oclass == ARMOR_CLASS && !strstri(d->bp, "mail")) {
        /* modifying bp's string is ok; we're about to resort
           to random armor if this also fails to match anything */
        Strcat(d->bp, " mail");
        return 6; /*goto retry;*/
    }
    if (!strcmpi(d->bp, "spinach")) {
        d->contents = SPINACH;
        d->typ = TIN;
        return 2; /*goto typfnd;*/
    }
    /* Fruits must not mess up the ability to wish for real objects (since
     * you can leave a fruit in a bones file and it will be added to
     * another person's game), so they must be checked for last, after
     * stripping all the possible prefixes and seeing if there's a real
     * name in there.  So we have to save the full original name.  However,
     * it's still possible to do things like "uncursed burnt Alaska",
     * or worse yet, "2 burned 5 course meals", so we need to loop to
     * strip off the prefixes again, this time stripping only the ones
     * possible on food.
     * We could get even more detailed so as to allow food names with
     * prefixes that _are_ possible on food, so you could wish for
     * "2 3 alarm chilis".  Currently this isn't allowed; options.c
     * automatically sticks 'candied' in front of such names.
     */
    /* Note: not strcmpi.  2 fruits, one capital, one not, are possible.
       Also not strncmp.  We used to ignore trailing text with it, but
       that resulted in "grapefruit" matching "grape" if the latter came
       earlier than the former in the fruit list. */
    {
        char *fp;
        int l, cntf;
        int blessedf, iscursedf, uncursedf, halfeatenf;
        struct fruit *f;

        blessedf = iscursedf = uncursedf = halfeatenf = 0;
        cntf = 0;

        fp = d->fruitbuf;
        for (;;) {
            if (!fp || !*fp)
                break;
            if (!strncmpi(fp, "an ", l = 3) || !strncmpi(fp, "a ", l = 2)) {
                cntf = 1;
            } else if (!cntf && digit(*fp)) {
                cntf = atoi(fp);
                while (digit(*fp))
                    fp++;
                while (*fp == ' ')
                    fp++;
                l = 0;
            } else if (!strncmpi(fp, "blessed ", l = 8)) {
                blessedf = 1;
            } else if (!strncmpi(fp, "cursed ", l = 7)) {
                iscursedf = 1;
            } else if (!strncmpi(fp, "uncursed ", l = 9)) {
                uncursedf = 1;
            } else if (!strncmpi(fp, "partly eaten ", l = 13)
                       || !strncmpi(fp, "partially eaten ", l = 16)) {
                halfeatenf = 1;
            } else
                break;
            fp += l;
        }

        for (f = g.ffruit; f; f = f->nextf) {
            /* match type: 0=none, 1=exact, 2=singular, 3=plural */
            int ftyp = 0;

            if (!strcmp(fp, f->fname))
                ftyp = 1;
            else if (!strcmp(fp, makesingular(f->fname)))
                ftyp = 2;
            else if (!strcmp(fp, makeplural(f->fname)))
                ftyp = 3;
            if (ftyp) {
                d->typ = SLIME_MOLD;
                d->blessed = blessedf;
                d->iscursed = iscursedf;
                d->uncursed = uncursedf;
                d->halfeaten = halfeatenf;
                /* adjust count if user explicitly asked for
                   singular amount (can't happen unless fruit
                   has been given an already pluralized name)
                   or for plural amount */
                if (ftyp == 2 && !cntf)
                    cntf = 1;
                else if (ftyp == 3 && !cntf)
                    cntf = 2;
                d->cnt = cntf;
                d->ftype = f->fid;
                return 2; /*goto typfnd;*/
            }
        }
    }

    if (!d->oclass && d->actualn) {
        short objtyp;

        /* Perhaps it's an artifact specified by name, not type */
        d->name = artifact_name(d->actualn, &objtyp, TRUE);
        if (d->name) {
            d->typ = objtyp;
            return 2; /*goto typfnd;*/
        }
    }

    return 0;
}


/*
 * Return something wished for.  Specifying a null pointer for
 * the user request string results in a random object.  Otherwise,
 * if asking explicitly for "nothing" (or "nil") return no_wish;
 * if not an object return &cg.zeroobj; if an error (no matching object),
 * return null.
 */
struct obj *
readobjnam(char *bp, struct obj *no_wish)
{
    struct _readobjnam_data d;

    readobjnam_init(bp, &d);
    if (!bp)
        goto any;

    /* first, remove extra whitespace they may have typed */
    (void) mungspaces(bp);
    /* allow wishing for "nothing" to preserve wishless conduct...
       [now requires "wand of nothing" if that's what was really wanted] */
    if (!strcmpi(bp, "nothing") || !strcmpi(bp, "nil")
        || !strcmpi(bp, "none"))
        return no_wish;
    /* save the [nearly] unmodified choice string */
    Strcpy(d.fruitbuf, bp);

    if (readobjnam_preparse(&d))
        goto any;

    if (!d.cnt)
        d.cnt = 1; /* will be changed to 2 if makesingular() changes string */

    readobjnam_parse_charges(&d);

    switch (readobjnam_postparse1(&d)) {
    default:
    case 0: break;
    case 1: goto srch;
    case 2: goto typfnd;
    case 3: return d.otmp;
    case 4: goto any;
    case 5: goto wiztrap;
    }

 retry:
    switch (readobjnam_postparse2(&d)) {
    default:
    case 0: break;
    case 1: goto srch;
    case 2: goto typfnd;
    case 3: return d.otmp;
    case 4: goto any;
    case 5: goto wiztrap;
    }

 srch:
    switch (readobjnam_postparse3(&d)) {
    default:
    case 0: break;
    case 1: goto srch;
    case 2: goto typfnd;
    case 3: return d.otmp;
    case 4: goto any;
    case 5: goto wiztrap;
    case 6: goto retry;
    }

    /*
     * Let wizards wish for traps and furniture.
     * Must come after objects check so wizards can still wish for
     * trap objects like beartraps.
     * Disallow such topology tweaks for WIZKIT startup wishes.
     */
 wiztrap:
    if (wizard && !g.program_state.wizkit_wishing && !d.oclass) {
        /* [inline code moved to separate routine to unclutter readobjnam] */
        if ((d.otmp = wizterrainwish(&d)) != 0)
            return d.otmp;
    }

    if (!d.oclass && !d.typ) {
        if (!strncmpi(d.bp, "polearm", 7)) {
            d.typ = rnd_otyp_by_wpnskill(P_POLEARMS);
            goto typfnd;
        } else if (!strncmpi(d.bp, "hammer", 6)) {
            d.typ = rnd_otyp_by_wpnskill(P_HAMMER);
            goto typfnd;
        }
    }

    if (!d.oclass)
        return ((struct obj *) 0);
 any:
    if (!d.oclass)
        d.oclass = wrpsym[rn2((int) sizeof wrpsym)];
 typfnd:
    if (d.typ)
        d.oclass = objects[d.typ].oc_class;

    /* handle some objects that are only allowed in wizard mode */
    if (d.typ && !wizard) {
        switch (d.typ) {
        case AMULET_OF_YENDOR:
            d.typ = FAKE_AMULET_OF_YENDOR;
            break;
        case CANDELABRUM_OF_INVOCATION:
            d.typ = rnd_class(TALLOW_CANDLE, WAX_CANDLE);
            break;
        case BELL_OF_OPENING:
            d.typ = BELL;
            break;
        case SPE_BOOK_OF_THE_DEAD:
            d.typ = SPE_BLANK_PAPER;
            break;
        case MAGIC_LAMP:
            d.typ = OIL_LAMP;
            break;
        default:
            /* catch any other non-wishable objects (venom) */
            if (objects[d.typ].oc_nowish)
                return (struct obj *) 0;
            break;
        }
    }

    /* if asking for corpse of a monster which leaves behind a glob, give
       glob instead of rejecting the monster type to create random corpse */
    if (d.typ == CORPSE && d.mntmp >= LOW_PM
        && mons[d.mntmp].mlet == S_PUDDING) {
        d.typ = GLOB_OF_GRAY_OOZE + (d.mntmp - PM_GRAY_OOZE);
        d.mntmp = NON_PM; /* not used for globs */
    }
    /*
     * Create the object, then fine-tune it.
     */
    d.otmp = d.typ ? mksobj(d.typ, TRUE, FALSE) : mkobj(d.oclass, FALSE);
    d.typ = d.otmp->otyp, d.oclass = d.otmp->oclass; /* what we actually got */

    if (d.islit && (d.typ == OIL_LAMP || d.typ == MAGIC_LAMP
                    || d.typ == BRASS_LANTERN
                    || Is_candle(d.otmp) || d.typ == POT_OIL)) {
        place_object(d.otmp, u.ux, u.uy); /* make it viable light source */
        begin_burn(d.otmp, FALSE);
        obj_extract_self(d.otmp); /* now release it for caller's use */
    }

    /* if player specified a reasonable count, maybe honor it;
       quantity for gold is handled elsewhere and d.cnt is 0 for it here */
    if (d.otmp->globby) {
        /* for globs, calculate weight based on gsize, then multiply by cnt;
           asking for 2 globs or for 2 small globs produces 1 small glob
           weighing 40au instead of normal 20au; asking for 5 medium globs
           might produce 1 very large glob weighing 600au */
        d.otmp->quan = 1L; /* always 1 for globs */
        d.otmp->owt = weight(d.otmp);
        /* gsize 0: unspecified => small;
           1: small (1..5) => keep default owt for 1, yielding 20;
           2: medium (6..15) => use weight for 6, yielding 120;
           3: large (16..25) => 320; 4: very large (26+) => 520 */
        if (d.gsize > 1)
            d.otmp->owt += ((unsigned) (5 + (d.gsize - 2) * 10)
                            * d.otmp->owt);  /* 20 + {5|15|25} times 20 */
        /* limit overall weight which limits shrink-away time which in turn
           affects how long some of it will remain available to be eaten */
        if (d.cnt > 1) {
            int rn1cnt = rn1(5, 2); /* 2..6 */

            if (rn1cnt > 6 - d.gsize)
                rn1cnt = 6 - d.gsize;
            if (d.cnt > rn1cnt
                && (!wizard || g.program_state.wizkit_wishing
                    || yn("Override glob weight limit?") != 'y'))
                d.cnt = rn1cnt;
            d.otmp->owt *= (unsigned) d.cnt;
        }
        /* note: the owt assignment below will not change glob's weight */
        d.cnt = 0;
    } else if (d.cnt > 0) {
        if (objects[d.typ].oc_merge
            && (wizard /* quantity isn't restricted when debugging */
                /* note: in normal play, explicitly asking for 1 might
                   fail the 'cnt < rnd(6)' test and could produce more
                   than 1 if mksobj() creates the item that way */
                || d.cnt < rnd(6)
                || (d.cnt <= 7 && Is_candle(d.otmp))
                || (d.cnt <= 20
                    && (d.typ == ROCK || d.typ == FLINT || is_missile(d.otmp)
                        /* WEAPON_CLASS test excludes gems, gray stones */
                        || (d.oclass == WEAPON_CLASS && is_ammo(d.otmp))))))
            d.otmp->quan = (long) d.cnt;
    }

    if (d.spesgn == 0) {
        /* spe not specifed; retain the randomly assigned value */
        d.spe = d.otmp->spe;
    } else if (wizard) {
        ; /* no restrictions except SPE_LIM */
    } else if (d.oclass == ARMOR_CLASS || d.oclass == WEAPON_CLASS
               || is_weptool(d.otmp)
               || (d.oclass == RING_CLASS && objects[d.typ].oc_charged)) {
        if (d.spe > rnd(5) && d.spe > d.otmp->spe)
            d.spe = 0;
        if (d.spe > 2 && Luck < 0)
            d.spesgn = -1;
    } else {
        /* crystal ball cancels like a wand, to (n:-1) */
        if (d.oclass == WAND_CLASS || d.typ == CRYSTAL_BALL) {
            if (d.spe > 1 && d.spesgn == -1)
                d.spe = 1;
        } else {
            if (d.spe > 0 && d.spesgn == -1)
                d.spe = 0;
        }
        if (d.spe > d.otmp->spe)
            d.spe = d.otmp->spe;
    }

    if (d.spesgn == -1)
        d.spe = -d.spe;

    /* set otmp->spe.  This may, or may not, use d.spe... */
    switch (d.typ) {
    case TIN:
        d.otmp->spe = 0; /* default: not spinach */
        if (d.contents == EMPTY) {
            d.otmp->corpsenm = NON_PM;
        } else if (d.contents == SPINACH) {
            d.otmp->corpsenm = NON_PM;
            d.otmp->spe = 1; /* spinach after all */
        }
        break;
    case TOWEL:
        if (d.wetness)
            d.otmp->spe = d.wetness;
        break;
    case SLIME_MOLD:
        d.otmp->spe = d.ftype;
    /* Fall through */
    case SKELETON_KEY:
    case CHEST:
    case LARGE_BOX:
    case HEAVY_IRON_BALL:
    case IRON_CHAIN:
        break;
    case STATUE: /* otmp->cobj already done in mksobj() */
    case FIGURINE:
    case CORPSE: {
        struct permonst *P = (d.mntmp >= LOW_PM) ? &mons[d.mntmp] : 0;

        d.otmp->spe = !P ? CORPSTAT_RANDOM
                      /* if neuter, force neuter regardless of wish request */
                      : is_neuter(P) ? CORPSTAT_NEUTER
                        /* not neuter, honor wish unless it conflicts */
                        : (d.mgend == FEMALE && !is_male(P)) ? CORPSTAT_FEMALE
                          : (d.mgend == MALE && !is_female(P)) ? CORPSTAT_MALE
                            /* unspecified or wish conflicts */
                            : CORPSTAT_RANDOM;
        if (P && d.otmp->spe == CORPSTAT_RANDOM)
            d.otmp->spe = is_male(P) ? CORPSTAT_MALE
                          : is_female(P) ? CORPSTAT_FEMALE
                            : rn2(2) ? CORPSTAT_MALE : CORPSTAT_FEMALE;
        if (d.ishistoric && d.typ == STATUE)
            d.otmp->spe |= CORPSTAT_HISTORIC;
        break;
    };
#ifdef MAIL_STRUCTURES
    /* scroll of mail:  0: delivered in-game via external event (or randomly
       for fake mail); 1: from bones or wishing; 2: written with marker */
    case SCR_MAIL:
        /*FALLTHRU*/
#endif
    /* splash of venom:  0: normal, and transitory; 1: wishing */
    case ACID_VENOM:
    case BLINDING_VENOM:
        d.otmp->spe = 1;
        break;
    case WAN_WISHING:
        if (!wizard) {
            d.otmp->spe = (rn2(10) ? -1 : 0);
            break;
        }
        /*FALLTHRU*/
    default:
        d.otmp->spe = d.spe;
    }

    /* set otmp->corpsenm or dragon scale [mail] */
    if (d.mntmp >= LOW_PM) {
        int humanwere;

        if (d.mntmp == PM_LONG_WORM_TAIL)
            d.mntmp = PM_LONG_WORM;
        /* werecreatures in beast form are all flagged no-corpse so for
           corpses and tins, switch to their corresponding human form;
           for figurines, override the can't-be-human restriction instead */
        if (d.typ != FIGURINE && is_were(&mons[d.mntmp])
            && (g.mvitals[d.mntmp].mvflags & G_NOCORPSE) != 0
            && (humanwere = counter_were(d.mntmp)) != NON_PM)
            d.mntmp = humanwere;

        switch (d.typ) {
        case TIN:
            if (dead_species(d.mntmp, FALSE)) {
                d.otmp->corpsenm = NON_PM; /* it's empty */
            } else if ((!(mons[d.mntmp].geno & G_UNIQ) || wizard)
                       && !(g.mvitals[d.mntmp].mvflags & G_NOCORPSE)
                       && mons[d.mntmp].cnutrit != 0) {
                d.otmp->corpsenm = d.mntmp;
            }
            break;
        case CORPSE:
            if ((!(mons[d.mntmp].geno & G_UNIQ) || wizard)
                && !(g.mvitals[d.mntmp].mvflags & G_NOCORPSE)) {
                if (mons[d.mntmp].msound == MS_GUARDIAN)
                    d.mntmp = genus(d.mntmp, 1);
                set_corpsenm(d.otmp, d.mntmp);
            }
            if (d.zombify && zombie_form(&mons[d.mntmp])) {
                (void) start_timer(rn1(5, 10), TIMER_OBJECT,
                                   ZOMBIFY_MON, obj_to_any(d.otmp));
            }
            break;
        case EGG:
            d.mntmp = can_be_hatched(d.mntmp);
            /* this also sets hatch timer if appropriate */
            set_corpsenm(d.otmp, d.mntmp);
            break;
        case FIGURINE:
            if (!(mons[d.mntmp].geno & G_UNIQ)
                && (!is_human(&mons[d.mntmp]) || is_were(&mons[d.mntmp]))
#ifdef MAIL_STRUCTURES
                && d.mntmp != PM_MAIL_DAEMON
#endif
                )
                d.otmp->corpsenm = d.mntmp;
            break;
        case STATUE:
            d.otmp->corpsenm = d.mntmp;
            if (Has_contents(d.otmp) && verysmall(&mons[d.mntmp]))
                delete_contents(d.otmp); /* no spellbook */
            break;
        case SCALE_MAIL:
            /* Dragon mail - depends on the order of objects & dragons. */
            if (d.mntmp >= PM_GRAY_DRAGON && d.mntmp <= PM_YELLOW_DRAGON)
                d.otmp->otyp = GRAY_DRAGON_SCALE_MAIL
                              + d.mntmp - PM_GRAY_DRAGON;
            break;
        }
    }

    /* set blessed/cursed -- setting the fields directly is safe
     * since weight() is called below and addinv() will take care
     * of luck */
    if (d.iscursed) {
        curse(d.otmp);
    } else if (d.uncursed) {
        d.otmp->blessed = 0;
        d.otmp->cursed = (Luck < 0 && !wizard);
    } else if (d.blessed) {
        d.otmp->blessed = (Luck >= 0 || wizard);
        d.otmp->cursed = (Luck < 0 && !wizard);
    } else if (d.spesgn < 0) {
        curse(d.otmp);
    }

    /* set eroded and erodeproof */
    if (erosion_matters(d.otmp)) {
        if (d.eroded && (is_flammable(d.otmp) || is_rustprone(d.otmp)))
            d.otmp->oeroded = d.eroded;
        if (d.eroded2 && (is_corrodeable(d.otmp) || is_rottable(d.otmp)))
            d.otmp->oeroded2 = d.eroded2;
        /*
         * 3.6.1: earlier versions included `&& !eroded && !eroded2' here,
         * but damageproof combined with damaged is feasible (eroded
         * armor modified by confused reading of cursed destroy armor)
         * so don't prevent player from wishing for such a combination.
         */
        if (d.erodeproof
            && (is_damageable(d.otmp) || d.otmp->otyp == CRYSKNIFE))
            d.otmp->oerodeproof = (Luck >= 0 || wizard);
    }

    /* set otmp->recharged */
    if (d.oclass == WAND_CLASS) {
        /* prevent wishing abuse */
        if (d.otmp->otyp == WAN_WISHING && !wizard)
            d.rechrg = 1;
        d.otmp->recharged = (unsigned) d.rechrg;
    }

    /* set poisoned */
    if (d.ispoisoned) {
        if (is_poisonable(d.otmp))
            d.otmp->opoisoned = (Luck >= 0);
        else if (d.oclass == FOOD_CLASS)
            /* try to taint by making it as old as possible */
            d.otmp->age = 1L;
    }
    /* and [un]trapped */
    if (d.trapped) {
        if (Is_box(d.otmp) || d.typ == TIN)
            d.otmp->otrapped = (d.trapped == 1);
    }
    /* empty for containers rather than for tins */
    if (d.contents == EMPTY) {
        if (d.otmp->otyp == BAG_OF_TRICKS || d.otmp->otyp == HORN_OF_PLENTY) {
            if (d.otmp->spe > 0)
                d.otmp->spe = 0;
        } else if (Has_contents(d.otmp)) {
            /* this assumes that artifacts can't be randomly generated
               inside containers */
            delete_contents(d.otmp);
            d.otmp->owt = weight(d.otmp);
        }
    }
    /* set locked/unlocked/broken */
    if (Is_box(d.otmp)) {
        if (d.locked) {
            d.otmp->olocked = 1, d.otmp->obroken = 0;
        } else if (d.unlocked) {
            d.otmp->olocked = 0, d.otmp->obroken = 0;
        } else if (d.broken) {
            d.otmp->olocked = 0, d.otmp->obroken = 1;
        }
        if (d.otmp->obroken)
            d.otmp->otrapped = 0;
    }

    if (d.isgreased)
        d.otmp->greased = 1;

    if (d.isdiluted && d.otmp->oclass == POTION_CLASS)
        d.otmp->odiluted = (d.otmp->otyp != POT_WATER);

    /* set tin variety */
    if (d.otmp->otyp == TIN && d.tvariety >= 0 && (rn2(4) || wizard))
        set_tin_variety(d.otmp, d.tvariety);

    if (d.name) {
        const char *aname, *novelname;
        short objtyp;

        /* an artifact name might need capitalization fixing */
        aname = artifact_name(d.name, &objtyp, TRUE);
        if (aname && objtyp == d.otmp->otyp)
            d.name = aname;

        /* 3.6 tribute - fix up novel */
        if (d.otmp->otyp == SPE_NOVEL
            && (novelname = lookup_novel(d.name, &d.otmp->novelidx)) != 0)
            d.name = novelname;

        d.otmp = oname(d.otmp, d.name, ONAME_WISH);
        /* name==aname => wished for artifact (otmp->oartifact => got it) */
        if (d.otmp->oartifact || d.name == aname) {
            d.otmp->quan = 1L;
            u.uconduct.wisharti++; /* KMH, conduct */
        }
    }

    /* more wishing abuse: don't allow wishing for certain artifacts */
    /* and make them pay; charge them for the wish anyway! */
    if ((is_quest_artifact(d.otmp)
         || (d.otmp->oartifact && rn2(nartifact_exist()) > 1)) && !wizard) {
        artifact_exists(d.otmp, safe_oname(d.otmp), FALSE, ONAME_NO_FLAGS);
        obfree(d.otmp, (struct obj *) 0);
        d.otmp = (struct obj *) &cg.zeroobj;
        pline("For a moment, you feel %s in your %s, but it disappears!",
              something, makeplural(body_part(HAND)));
        return d.otmp;
    }

    if (d.halfeaten && d.otmp->oclass == FOOD_CLASS) {
        unsigned nut = obj_nutrition(d.otmp);

        /* do this adjustment before setting up object's weight; skip
           "partly eaten" for food with 0 nutrition (wraith corpse) or for
           anything that couldn't take more than one bite (1 nutrition;
           ought to check for one-bite instead but that's complicated) */
        if (nut > 1) {
            d.otmp->oeaten = nut;
            consume_oeaten(d.otmp, 1);
        }
    }
    d.otmp->owt = weight(d.otmp);
    if (d.very && d.otmp->otyp == HEAVY_IRON_BALL)
        d.otmp->owt += IRON_BALL_W_INCR;

    return d.otmp;
}

/* compare user string against object name string using fuzzy matching */
boolean
wishymatch(
    const char *u_str,      /* from user, so might be variant spelling */
    const char *o_str,      /* from objects[], so is in canonical form */
    boolean retry_inverted) /* optional extra "of" handling */
{
    static NEARDATA const char detect_SP[] = "detect ",
                               SP_detection[] = " detection";
    char *p, buf[BUFSZ];

    /* ignore spaces & hyphens and upper/lower case when comparing */
    if (fuzzymatch(u_str, o_str, " -", TRUE))
        return TRUE;

    if (retry_inverted) {
        const char *u_of, *o_of;

        /* when just one of the strings is in the form "foo of bar",
           convert it into "bar foo" and perform another comparison */
        u_of = strstri(u_str, " of ");
        o_of = strstri(o_str, " of ");
        if (u_of && !o_of) {
            Strcpy(buf, u_of + 4);
            copynchars(eos(strcat(buf, " ")), u_str, (int) (u_of - u_str));
            if (fuzzymatch(buf, o_str, " -", TRUE))
                return TRUE;
        } else if (o_of && !u_of) {
            Strcpy(buf, o_of + 4);
            copynchars(eos(strcat(buf, " ")), o_str, (int) (o_of - o_str));
            if (fuzzymatch(u_str, buf, " -", TRUE))
                return TRUE;
        }
    }

    /* [note: if something like "elven speed boots" ever gets added, these
       special cases should be changed to call wishymatch() recursively in
       order to get the "of" inversion handling] */
    if (!strncmp(o_str, "dwarvish ", 9)) {
        if (!strncmpi(u_str, "dwarven ", 8))
            return fuzzymatch(u_str + 8, o_str + 9, " -", TRUE);
    } else if (!strncmp(o_str, "elven ", 6)) {
        if (!strncmpi(u_str, "elvish ", 7))
            return fuzzymatch(u_str + 7, o_str + 6, " -", TRUE);
        else if (!strncmpi(u_str, "elfin ", 6))
            return fuzzymatch(u_str + 6, o_str + 6, " -", TRUE);
    } else if (strstri(o_str, "helm") && strstri(u_str, "helmet")) {
        copynchars(buf, u_str, (int) sizeof buf - 1);
        (void) strsubst(buf, "helmet", "helm");
        return wishymatch(buf, o_str,  TRUE);
    } else if (strstri(o_str, "gauntlets") && strstri(u_str, "gloves")) {
        /* -3: room to replace shorter "gloves" with longer "gauntlets" */
        copynchars(buf, u_str, (int) sizeof buf - 1 - 3);
        (void) strsubst(buf, "gloves", "gauntlets");
        return wishymatch(buf, o_str, TRUE);
    } else if (!strncmp(o_str, detect_SP, sizeof detect_SP - 1)) {
        /* check for "detect <foo>" vs "<foo> detection" */
        if ((p = strstri(u_str, SP_detection)) != 0
            && !*(p + sizeof SP_detection - 1)) {
            /* convert "<foo> detection" into "detect <foo>" */
            *p = '\0';
            Strcat(strcpy(buf, detect_SP), u_str);
            /* "detect monster" -> "detect monsters" */
            if (!strcmpi(u_str, "monster"))
                Strcat(buf, "s");
            *p = ' ';
            return fuzzymatch(buf, o_str, " -", TRUE);
        }
    } else if (strstri(o_str, SP_detection)) {
        /* and the inverse, "<foo> detection" vs "detect <foo>" */
        if (!strncmpi(u_str, detect_SP, sizeof detect_SP - 1)) {
            /* convert "detect <foo>s" into "<foo> detection" */
            p = makesingular(u_str + sizeof detect_SP - 1);
            Strcat(strcpy(buf, p), SP_detection);
            /* caller may be looping through objects[], so avoid
               churning through all the obufs */
            maybereleaseobuf(p);
            return fuzzymatch(buf, o_str, " -", TRUE);
        }
    } else if (strstri(o_str, "ability")) {
        /* when presented with "foo of bar", makesingular() used to
           singularize both foo & bar, but now only does so for foo */
        /* catch "{potion(s),ring} of {gain,restore,sustain} abilities" */
        if ((p = strstri(u_str, "abilities")) != 0
            && !*(p + sizeof "abilities" - 1)) {
            (void) strncpy(buf, u_str, (unsigned) (p - u_str));
            Strcpy(buf + (p - u_str), "ability");
            return fuzzymatch(buf, o_str, " -", TRUE);
        }
    } else if (!strcmp(o_str, "aluminum")) {
        /* this special case doesn't really fit anywhere else... */
        /* (note that " wand" will have been stripped off by now) */
        if (!strcmpi(u_str, "aluminium"))
            return fuzzymatch(u_str + 9, o_str + 8, " -", TRUE);
    }

    return FALSE;
}

static short
rnd_otyp_by_wpnskill(schar skill)
{
    int i, n = 0;
    short otyp = STRANGE_OBJECT;

    for (i = g.bases[WEAPON_CLASS];
         i < NUM_OBJECTS && objects[i].oc_class == WEAPON_CLASS; i++)
        if (objects[i].oc_skill == skill) {
            n++;
            otyp = i;
        }
    if (n > 0) {
        n = rn2(n);
        for (i = g.bases[WEAPON_CLASS];
             i < NUM_OBJECTS && objects[i].oc_class == WEAPON_CLASS; i++)
            if (objects[i].oc_skill == skill)
                if (--n < 0)
                    return i;
    }
    return otyp;
}

static short
rnd_otyp_by_namedesc(
    const char *name,
    char oclass,
    int xtra_prob) /* add to item's chance of being chosen; non-zero causes
                    * 0% random generation items to also be considered */
{
    int i, n = 0;
    short validobjs[NUM_OBJECTS];
    register const char *zn, *of;
    boolean check_of;
    int lo, hi, minglob, maxglob, prob, maxprob = 0;

    if (!name || !*name)
        return STRANGE_OBJECT;

    /* only skip "foo of" for "foo of bar" if target doesn't contain " of " */
    check_of = (strstri(name, " of ") == 0);
    minglob = GLOB_OF_GRAY_OOZE;
    maxglob = GLOB_OF_BLACK_PUDDING;

    (void) memset((genericptr_t) validobjs, 0, sizeof validobjs);
    if (oclass) {
        lo = g.bases[(uchar) oclass];
        hi = g.bases[(uchar) oclass + 1] - 1;
    } else {
        lo = STRANGE_OBJECT + 1;
        hi = NUM_OBJECTS - 1;
    }
    /* FIXME:
     * When this spans classes (the !oclass case), the item
     * probabilities are not very useful because they don't take
     * the class generation probability into account.  [If 10%
     * of spellbooks were blank and 1% of scrolls were blank,
     * "blank" would have 10/11 chance to yield a book even though
     * scrolls are supposed to be much more common than books.]
     */
    for (i = lo; i <= hi; ++i) {
        /* don't match extra descriptions (w/o real name) */
        if ((zn = OBJ_NAME(objects[i])) == 0)
            continue;
        if (wishymatch(name, zn, TRUE) /* objects[] name */
            /* let "<bar>" match "<foo> of <bar>" (already does if foo is
               an object class, but this is for lump of royal jelly,
               clove of garlic, bag of tricks, &c) with a few exceptions:
               for "opening", don't match "bell of opening"; for monster
               type ooze/pudding/slime don't match glob of same since that
               ought to match "corpse/egg/figurine of type" too but won't */
            || (check_of
                && i != BELL_OF_OPENING && i != HUGE_CHUNK_OF_MEAT
                && (i < minglob || i > maxglob)
                && (of = strstri(zn, " of ")) != 0
                && wishymatch(name, of + 4, FALSE)) /* partial name */
            || ((zn = OBJ_DESCR(objects[i])) != 0
                && wishymatch(name, zn, FALSE)) /* objects[] description */
            /* "cloth" should match "piece of cloth"; there's only one
               description containing " of " so no special case handling */
            || (zn && check_of && (of = strstri(zn, " of ")) != 0
                && wishymatch(name, of + 4, FALSE)) /* partial description */
            || ((zn = objects[i].oc_uname) != 0
                && wishymatch(name, zn, FALSE)) /* user-called name */
            ) {
            validobjs[n++] = (short) i;
            maxprob += (objects[i].oc_prob + xtra_prob);
        }
    }

    if (n > 0 && maxprob) {
        prob = rn2(maxprob);
        for (i = 0; i < n - 1; i++)
            if ((prob -= (objects[validobjs[i]].oc_prob + xtra_prob)) < 0)
                break;
        return validobjs[i];
    }
    return STRANGE_OBJECT;
}

int
shiny_obj(char oclass)
{
    return (int) rnd_otyp_by_namedesc("shiny", oclass, 0);
}

/*wish.c*/
