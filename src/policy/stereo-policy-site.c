/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/* The site's mark for a video, read from the keywords of its page: YouTube's yt3d tags (docs/yt3d.md). One reader for the
 * browser and for PlasmaTube, so a video is read alike wherever it is shown. */

#include "stereo-policy.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

#define TAG_MAX 256

/* What the first form, yt3d:enable=true, means without an arrangement of its own: side by side, right eye first. YouTube's
 * player of 2010 took such a video for cross-eyed (the right eye's view on the left) and showed a parallel one (the left eye's
 * view on the left) right only with yt3d:swap=true: "the yt3d:swap tag was still necessary to get the player to recognize the
 * left eye frame was on the left" (stereo3dchannel.blogspot.com, 2011-02-21); Tim Dashwood on dvinfo.net: "if your video is in
 * parallel format just add the tag yt3d:swap=true". Measured on "3D Maestro" (uMTvaNomlWY, enable=true and swap=true): the left
 * half holds the left eye's view (docs/yt3d.md). swap reverses whatever else decided, so enable=true with swap=true is left first. */
#define GENERIC_ORDER STEREO_POLICY_RIGHT_FIRST

enum enable_state { ENABLE_NONE, ENABLE_PACKING, ENABLE_GENERIC, ENABLE_OFF, ENABLE_UNKNOWN };

struct rect {
    double l, t, r, b;
    int set;
};

struct site {
    int seen; /* a yt3d tag was met */
    enum enable_state enable;
    int packing_layout, packing_order;
    int swap;
    struct rect left, right;
    int left_bad, right_bad;
    double aspect_num, aspect_den;
};

static void set(int *target, int value)
{
    if (target)
        *target = value;
}

/* A number as far as it reads: digits, a decimal point or comma, digits; "0." and ".5" are numbers. Returns the end, or null. */
static const char *read_number(const char *p, double *out)
{
    double value = 0, scale = 1;
    int digits = 0, fraction = 0;
    for (;; ++p) {
        if (*p >= '0' && *p <= '9') {
            ++digits;
            if (fraction) {
                scale /= 10;
                value += (*p - '0') * scale;
            } else {
                value = value * 10 + (*p - '0');
            }
        } else if ((*p == '.' || *p == ',') && !fraction) {
            fraction = 1;
        } else {
            break;
        }
    }
    if (!digits)
        return NULL;
    *out = value;
    return p;
}

static int is_true(const char *v) { return strcmp(v, "true") == 0 || strcmp(v, "1") == 0 || strcmp(v, "yes") == 0 || strcmp(v, "on") == 0; }
static int is_false(const char *v) { return strcmp(v, "false") == 0 || strcmp(v, "0") == 0 || strcmp(v, "no") == 0 || strcmp(v, "off") == 0; }

/* l_t_r_b: four numbers split at anything that is not part of a number (an underscore, a semicolon, a space) */
static int read_rect(const char *value, struct rect *rect)
{
    double n[4];
    int count = 0;
    const char *p = value;
    while (*p && count < 5) {
        while (*p && !((*p >= '0' && *p <= '9') || *p == '.' || *p == ','))
            ++p;
        if (!*p)
            break;
        double v;
        const char *end = read_number(p, &v);
        if (!end)
            break;
        if (count < 4)
            n[count] = v;
        ++count;
        p = end;
    }
    if (count != 4)
        return 0;
    rect->l = n[0];
    rect->t = n[1];
    rect->r = n[2];
    rect->b = n[3];
    rect->set = 1;
    return 1;
}

static int read_aspect(const char *value, double *num, double *den)
{
    double a, b = 1;
    const char *p = read_number(value, &a);
    if (!p || a <= 0)
        return 0;
    if (*p == ':' || *p == '/' || *p == 'x') {
        if (!(p = read_number(p + 1, &b)) || b <= 0)
            return 0;
    }
    if (a / b < 0.1 || a / b > 10)
        return 0;
    *num = a;
    *den = b;
    return 1;
}

static void feed(struct site *s, const char *keyword)
{
    char tag[TAG_MAX];
    size_t length = 0;
    if (!keyword)
        return;
    /* lower case, without quotes and without the spaces around the name and the value: " YT3D:Enable = LR " is yt3d:enable=lr;
     * the spaces inside a value stay, a rectangle may be written "0 0 0.5 1" */
    for (const char *p = keyword; *p; ++p) {
        if (*p == '"' || *p == '\'' || *p == '\r' || *p == '\n' || *p == '\t')
            continue;
        if (length + 1 >= TAG_MAX)
            return;
        tag[length++] = (char)tolower((unsigned char)*p);
    }
    tag[length] = 0;
    char *equals = strchr(tag, '=');
    char *start = tag;
    while (*start == ' ')
        ++start;
    /* the name before the equals sign has no spaces at all */
    char name_part[TAG_MAX];
    size_t n = 0;
    for (const char *p = start; *p && p != equals; ++p)
        if (*p != ' ')
            name_part[n++] = *p;
    name_part[n] = 0;
    if (strncmp(name_part, "yt3d:", 5) != 0)
        return;
    char value_part[TAG_MAX];
    value_part[0] = 0;
    if (equals) {
        const char *v = equals + 1;
        while (*v == ' ')
            ++v;
        size_t m = strlen(v);
        while (m && v[m - 1] == ' ')
            --m;
        memcpy(value_part, v, m);
        value_part[m] = 0;
    }
    s->seen = 1;
    const char *name = name_part + 5;
    const char *value = equals ? value_part : NULL;
    if (strcmp(name, "enable") == 0 && value) {
        if (strcmp(value, "lr") == 0 || strcmp(value, "rl") == 0) {
            s->enable = ENABLE_PACKING;
            s->packing_layout = STEREO_POLICY_LAYOUT_SIDE_BY_SIDE;
            s->packing_order = value[0] == 'r' ? STEREO_POLICY_RIGHT_FIRST : STEREO_POLICY_LEFT_FIRST;
        } else if (strcmp(value, "lonr") == 0 || strcmp(value, "ronl") == 0) {
            s->enable = ENABLE_PACKING;
            s->packing_layout = STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM;
            s->packing_order = value[0] == 'r' ? STEREO_POLICY_RIGHT_FIRST : STEREO_POLICY_LEFT_FIRST;
        } else if (is_true(value)) {
            if (s->enable != ENABLE_PACKING)
                s->enable = ENABLE_GENERIC;
        } else if (is_false(value)) {
            if (s->enable != ENABLE_PACKING)
                s->enable = ENABLE_OFF;
        } else if (s->enable == ENABLE_NONE) {
            s->enable = ENABLE_UNKNOWN;
        }
    } else if (strcmp(name, "swap") == 0) {
        s->swap = !value || is_true(value);
    } else if (strcmp(name, "left") == 0 && value) {
        s->left_bad = !read_rect(value, &s->left);
    } else if (strcmp(name, "right") == 0 && value) {
        s->right_bad = !read_rect(value, &s->right);
    } else if (strcmp(name, "aspect") == 0 && value) {
        read_aspect(value, &s->aspect_num, &s->aspect_den);
    }
}

static int near(double a, double b) { return a - b < 0.02 && b - a < 0.02; }

/* where the two rectangles put the eyes: the halves of the frame, side by side or one above the other */
static int rect_arrangement(const struct site *s, int *layout, int *order)
{
    const struct rect *a = &s->left, *b = &s->right;
    if (!a->set || !b->set)
        return 0;
    if (near(a->t, 0) && near(a->b, 1) && near(b->t, 0) && near(b->b, 1)) {
        if (near(a->l, 0) && near(a->r, 0.5) && near(b->l, 0.5) && near(b->r, 1)) {
            *layout = STEREO_POLICY_LAYOUT_SIDE_BY_SIDE;
            *order = STEREO_POLICY_LEFT_FIRST;
            return 1;
        }
        if (near(a->l, 0.5) && near(a->r, 1) && near(b->l, 0) && near(b->r, 0.5)) {
            *layout = STEREO_POLICY_LAYOUT_SIDE_BY_SIDE;
            *order = STEREO_POLICY_RIGHT_FIRST;
            return 1;
        }
    }
    if (near(a->l, 0) && near(a->r, 1) && near(b->l, 0) && near(b->r, 1)) {
        if (near(a->t, 0) && near(a->b, 0.5) && near(b->t, 0.5) && near(b->b, 1)) {
            *layout = STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM;
            *order = STEREO_POLICY_LEFT_FIRST;
            return 1;
        }
        if (near(a->t, 0.5) && near(a->b, 1) && near(b->t, 0) && near(b->b, 0.5)) {
            *layout = STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM;
            *order = STEREO_POLICY_RIGHT_FIRST;
            return 1;
        }
    }
    return 0;
}

static int finish(const struct site *s, int *layout, int *order, int *aspect_num, int *aspect_den)
{
    set(layout, STEREO_POLICY_LAYOUT_NONE);
    set(order, STEREO_POLICY_LEFT_FIRST);
    set(aspect_num, 0);
    set(aspect_den, 0);
    if (!s->seen)
        return 0;
    int l = STEREO_POLICY_LAYOUT_NONE, o = STEREO_POLICY_LEFT_FIRST;
    if (s->enable == ENABLE_OFF) {
        return 1; /* the site says the video is not 3D */
    } else if (s->enable == ENABLE_PACKING) {
        l = s->packing_layout;
        o = s->packing_order;
    } else if (rect_arrangement(s, &l, &o)) {
        /* the rectangles say where the eyes are, with or without the first form's enable=true */
    } else if (s->enable == ENABLE_GENERIC && !s->left.set && !s->right.set) {
        l = STEREO_POLICY_LAYOUT_SIDE_BY_SIDE;
        o = GENERIC_ORDER;
    } else {
        return 2; /* yt3d tags, but no arrangement this policy has */
    }
    if (s->swap)
        o = o == STEREO_POLICY_LEFT_FIRST ? STEREO_POLICY_RIGHT_FIRST : STEREO_POLICY_LEFT_FIRST;
    set(layout, l);
    set(order, o);
    if (s->aspect_num > 0 && s->aspect_den > 0) {
        set(aspect_num, (int)(s->aspect_num * 1000 + 0.5));
        set(aspect_den, (int)(s->aspect_den * 1000 + 0.5));
    }
    return 1;
}

int stereo_policy_site_mark(const char *const *keywords, int count, int *layout, int *order, int *aspect_num, int *aspect_den)
{
    if (count < 0 || (count > 0 && !keywords)) {
        set(layout, STEREO_POLICY_LAYOUT_NONE);
        return -1;
    }
    struct site s;
    memset(&s, 0, sizeof(s));
    for (int i = 0; i < count; ++i)
        feed(&s, keywords[i]);
    return finish(&s, layout, order, aspect_num, aspect_den);
}

int stereo_policy_site_mark_list(const char *keywords, int *layout, int *order, int *aspect_num, int *aspect_den)
{
    if (!keywords) {
        set(layout, STEREO_POLICY_LAYOUT_NONE);
        return -1;
    }
    struct site s;
    memset(&s, 0, sizeof(s));
    char tag[TAG_MAX];
    size_t length = 0;
    int overlong = 0;
    for (const char *p = keywords;; ++p) {
        if (*p == ',' || *p == '\n' || *p == 0) {
            if (!overlong) {
                tag[length] = 0;
                feed(&s, tag);
            }
            length = 0;
            overlong = 0;
            if (!*p)
                break;
        } else if (length + 1 < TAG_MAX) {
            tag[length++] = *p;
        } else {
            overlong = 1;
        }
    }
    return finish(&s, layout, order, aspect_num, aspect_den);
}

int stereo_policy_site_mark_word(int layout, int order, const char **word)
{
    const int right = order == STEREO_POLICY_RIGHT_FIRST;
    if (!word)
        return -1;
    switch (layout) {
    case STEREO_POLICY_LAYOUT_NONE:
        *word = "mono";
        return 1;
    case STEREO_POLICY_LAYOUT_SIDE_BY_SIDE:
        *word = right ? "sideBySideRightFirst" : "sideBySideLeftFirst";
        return 1;
    case STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM:
        *word = right ? "topBottomRightFirst" : "topBottomLeftFirst";
        return 1;
    default:
        return -1;
    }
}
