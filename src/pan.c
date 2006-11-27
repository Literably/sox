/*
 * (c) 20/03/2000 Fabien COELHO <fabien@coelho.net> for sox.
 *
 * Same copyright as SOX. See Sox (and Sun?;-)
 *
 * Change panorama of sound file with basic linear volume interpolation.
 * The human ear is not sensible to phases? What about delay? too short?
 * 
 * Volume is kept constant (?). 
 * Beware of saturations!
 * Operations are carried out on doubles.
 * Can handle different number of channels.
 * Cannot handle rate change.
 *
 * Initially based on avg effect. 
 * pan 0.0 basically behaves as avg.
 */

#include "st_i.h"

static st_effect_t st_pan_effect;

/* structure to hold pan parameter */

typedef struct {
    double dir; /* direction, from left (-1.0) to right (1.0) */
} * pan_t;

/*
 * Process options
 */
int st_pan_getopts(eff_t effp, int n, char **argv) 
{
    pan_t pan = (pan_t) effp->priv; 
    
    pan->dir = 0.0; /* default is no change */
    
    if (n && (!sscanf(argv[0], "%lf", &pan->dir) || 
              pan->dir < -1.0 || pan->dir > 1.0))
    {
        st_fail(st_pan_effect.usage);
        return ST_EOF;
    }

    return ST_SUCCESS;
}

/*
 * Start processing
 */
int st_pan_start(eff_t effp)
{
    if (effp->outinfo.channels==1)
        st_warn("PAN onto a mono channel...");

    if (effp->outinfo.rate != effp->ininfo.rate)
    {
        st_fail("PAN cannot handle different rates (in=%ld, out=%ld)"
             " use resample or rate", effp->ininfo.rate, effp->outinfo.rate);
        return ST_EOF;
    }

    return ST_SUCCESS;
}


#define UNEXPECTED_CHANNELS \
    st_fail("unexpected number of channels (in=%d, out=%d)", ich, och); \
    return ST_EOF

/*
 * Process either isamp or osamp samples, whichever is smaller.
 */
int st_pan_flow(eff_t effp, st_sample_t *ibuf, st_sample_t *obuf, 
                st_size_t *isamp, st_size_t *osamp)
{
    pan_t pan = (pan_t) effp->priv;
    register st_size_t len;
    st_size_t done;
    char ich, och;
    register double left, right, dir, hdir;
    
    dir   = pan->dir;    /* -1   <=  dir  <= 1   */
    hdir  = 0.5 * dir;  /* -0.5 <=  hdir <= 0.5 */
    left  = 0.5 - hdir; /*  0   <=  left <= 1   */
    right = 0.5 + hdir; /*  0   <= right <= 1   */

    ich = effp->ininfo.channels;
    och = effp->outinfo.channels;

    len = min(*osamp/och,*isamp/ich);

    /* report back how much is processed. */
    *isamp = len*ich;
    *osamp = len*och;
    
    /* 9 different cases to handle: (1,2,4) X (1,2,4) */
    switch (och) {
    case 1: /* pan on mono channel... not much sense. just avg. */
        switch (ich) {
        case 1: /* simple copy */
            for (done=0; done<len; done++)
                *obuf++ = *ibuf++;
            break;
        case 2: /* average 2 */
            for (done=0; done<len; done++)
            {
                double f;
                f = 0.5*ibuf[0] + 0.5*ibuf[1];
                ST_EFF_SAMPLE_CLIP_COUNT(f);
                *obuf++ = f;
                ibuf += 2;
            }
            break;
        case 4: /* average 4 */
            for (done=0; done<len; done++)
            {
                double f;
                f = 0.25*ibuf[0] + 0.25*ibuf[1] + 
                        0.25*ibuf[2] + 0.25*ibuf[3];
                ST_EFF_SAMPLE_CLIP_COUNT(f);
                *obuf++ = f;
                ibuf += 4;
            }
            break;
        default:
            UNEXPECTED_CHANNELS;
            break;
        } /* end first switch in channel */
        break;
    case 2:
        switch (ich) {
        case 1: /* linear */
            for (done=0; done<len; done++)
            {
                double f;

                f = left * ibuf[0];
                ST_EFF_SAMPLE_CLIP_COUNT(f);
                obuf[0] = f;
                f = right * ibuf[0];
                ST_EFF_SAMPLE_CLIP_COUNT(f);
                obuf[1] = f;
                obuf += 2;
                ibuf++;
            }
            break;
        case 2: /* linear panorama. 
                 * I'm not sure this is the right way to do it.
                 */
            if (dir <= 0.0) /* to the left */
            {
                register double volume, cll, clr, cr;

                volume = 1.0 - 0.5*dir;
                cll = volume*(1.5-left);
                clr = volume*(left-0.5);
                cr  = volume*(1.0+dir);

                for (done=0; done<len; done++)
                {
                    double f;

                    f = cll * ibuf[0] + clr * ibuf[1];
                    ST_EFF_SAMPLE_CLIP_COUNT(f);
                    obuf[0] = f;
                    f = cr * ibuf[1];
                    ST_EFF_SAMPLE_CLIP_COUNT(f);
                    obuf[1] = f;
                    obuf += 2;
                    ibuf += 2;
                }
            }
            else /* to the right */
            {
                register double volume, cl, crl, crr;

                volume = 1.0 + 0.5*dir;
                cl  = volume*(1.0-dir);
                crl = volume*(right-0.5);
                crr = volume*(1.5-right);

                for (done=0; done<len; done++)
                {
                    double f;

                    f = cl * ibuf[0];
                    ST_EFF_SAMPLE_CLIP_COUNT(f);
                    obuf[0] = f;
                    f = crl * ibuf[0] + crr * ibuf[1];
                    ST_EFF_SAMPLE_CLIP_COUNT(f);
                    obuf[1] = f;
                    obuf += 2;
                    ibuf += 2;
                }
            }
            break;
        case 4:
            if (dir <= 0.0) /* to the left */
            {
                register double volume, cll, clr, cr;

                volume = 1.0 - 0.5*dir;
                cll = volume*(1.5-left);
                clr = volume*(left-0.5);
                cr  = volume*(1.0+dir);

                for (done=0; done<len; done++)
                {
                    register double ibuf0, ibuf1, f;

                    /* build stereo signal */
                    ibuf0 = 0.5*ibuf[0] + 0.5*ibuf[2];
                    ibuf1 = 0.5*ibuf[1] + 0.5*ibuf[3];

                    /* pan it */
                    f = cll * ibuf0 + clr * ibuf1;
                    ST_EFF_SAMPLE_CLIP_COUNT(f);
                    obuf[0] = f;
                    f = cr * ibuf1;
                    ST_EFF_SAMPLE_CLIP_COUNT(f);
                    obuf[1] = f;
                    obuf += 2;
                    ibuf += 4;
                }
            }
            else /* to the right */
            {
                register double volume, cl, crl, crr;

                volume = 1.0 + 0.5*dir;
                cl  = volume*(1.0-dir);
                crl = volume*(right-0.5);
                crr = volume*(1.5-right);

                for (done=0; done<len; done++)
                {
                    register double ibuf0, ibuf1, f;

                    ibuf0 = 0.5*ibuf[0] + 0.5*ibuf[2];
                    ibuf1 = 0.5*ibuf[1] + 0.5*ibuf[3];

                    f = cl * ibuf0;
                    ST_EFF_SAMPLE_CLIP_COUNT(f);
                    obuf[0] = f;
                    f = crl * ibuf0 + crr * ibuf1;
                    ST_EFF_SAMPLE_CLIP_COUNT(f);
                    obuf[1] = f;
                    obuf += 2;
                    ibuf += 4;
                }
            }
            break;
        default:
            UNEXPECTED_CHANNELS;
            break;
        } /* end second switch in channel */
        break;
    case 4:
        switch (ich) {
        case 1: /* linear */
            {
                register double cr, cl;

                cl = 0.5*left;
                cr = 0.5*right;

                for (done=0; done<len; done++)
                {
                    double f;

                    f = cl * ibuf[0];
                    ST_EFF_SAMPLE_CLIP_COUNT(f);
                    obuf[2] = obuf[0] = f;
                    f = cr * ibuf[0];
                    ST_EFF_SAMPLE_CLIP_COUNT(f);
                    ibuf[3] = obuf[1] = f;
                    obuf += 4;
                    ibuf++;
                }
            }
            break;
        case 2: /* simple linear panorama */
            if (dir <= 0.0) /* to the left */
            {
                register double volume, cll, clr, cr;

                volume = 0.5 - 0.25*dir;
                cll = volume * (1.5-left);
                clr = volume * (left-0.5);
                cr  = volume * (1.0+dir);

                for (done=0; done<len; done++)
                {
                    double f;

                    f = cll * ibuf[0] + clr * ibuf[1];
                    ST_EFF_SAMPLE_CLIP_COUNT(f);
                    obuf[2] = obuf[0] = f;
                    f = cr * ibuf[1];
                    ST_EFF_SAMPLE_CLIP_COUNT(f);
                    ibuf[3] = obuf[1] = f;
                    obuf += 4;
                    ibuf += 2;
                }
            }
            else /* to the right */
            {
                register double volume, cl, crl, crr;

                volume = 0.5 + 0.25*dir;
                cl  = volume * (1.0-dir);
                crl = volume * (right-0.5);
                crr = volume * (1.5-right);

                for (done=0; done<len; done++)
                {
                    double f;

                    f = cl * ibuf[0];
                    ST_EFF_SAMPLE_CLIP_COUNT(f);
                    obuf[2] = obuf[0] =f ;
                    f = crl * ibuf[0] + crr * ibuf[1];
                    ST_EFF_SAMPLE_CLIP_COUNT(f);
                    ibuf[3] = obuf[1] = f;
                    obuf += 4;
                    ibuf += 2;
                }
            }
            break;
        case 4:
            /* maybe I could improve the formula to reverse...
               also, turn only by quarters.
             */
            if (dir <= 0.0) /* to the left */
            {
                register double cown, cright;

                cright = -dir;
                cown = 1.0 + dir;

                for (done=0; done<len; done++)
                {
                    double f;

                    f = cown*ibuf[0] + cright*ibuf[1];
                    ST_EFF_SAMPLE_CLIP_COUNT(f);
                    obuf[0] = f;
                    f = cown*ibuf[1] + cright*ibuf[3];
                    ST_EFF_SAMPLE_CLIP_COUNT(f);
                    obuf[1] = f;
                    f = cown*ibuf[2] + cright*ibuf[0];
                    ST_EFF_SAMPLE_CLIP_COUNT(f);
                    obuf[2] = f;
                    f = cown*ibuf[3] + cright*ibuf[2];
                    ST_EFF_SAMPLE_CLIP_COUNT(f);
                    obuf[3] = f;
                    obuf += 4;
                    ibuf += 4;              
                }
            }
            else /* to the right */
            {
                register double cleft, cown;

                cleft = dir;
                cown = 1.0 - dir;

                for (done=0; done<len; done++)
                {
                    double f;

                    f = cleft*ibuf[2] + cown*ibuf[0];
                    ST_EFF_SAMPLE_CLIP_COUNT(f);
                    obuf[0] = f;
                    f = cleft*ibuf[0] + cown*ibuf[1];
                    ST_EFF_SAMPLE_CLIP_COUNT(f);
                    obuf[1] = f;
                    f = cleft*ibuf[3] + cown*ibuf[2];
                    ST_EFF_SAMPLE_CLIP_COUNT(f);
                    obuf[2] = f;
                    f = cleft*ibuf[1] + cown*ibuf[3];
                    ST_EFF_SAMPLE_CLIP_COUNT(f);
                    obuf[3] = f;
                    obuf += 4;
                    ibuf += 4;
                }
            }
            break;
        default:
            UNEXPECTED_CHANNELS;
            break;
        } /* end third switch in channel */
        break;
    default:
        UNEXPECTED_CHANNELS;
        break;
    } /* end switch out channel */

    return ST_SUCCESS;
}

/*
 * Do anything required when you stop reading samples.  
 * Don't close input file! 
 *
 * Should have statistics on right, left, and output amplitudes.
 */
int st_pan_stop(eff_t effp)
{
    return ST_SUCCESS;
}

static st_effect_t st_pan_effect = {
  "pan",
  "Usage: pan direction (in [-1.0 .. 1.0])",
  ST_EFF_MCHAN | ST_EFF_CHAN,
  st_pan_getopts,
  st_pan_start,
  st_pan_flow,
  st_effect_nothing_drain,
  st_pan_stop
};

const st_effect_t *st_pan_effect_fn(void)
{
    return &st_pan_effect;
}
