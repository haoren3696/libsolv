#include <sys/types.h>
#include <limits.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "pool.h"
#include "source_content.h"

#define PACK_BLOCK 16

static int
split(char *l, char **sp, int m)
{
  int i;
  for (i = 0; i < m;)
    {
      while (*l == ' ' || *l == '\t')
	l++;
      if (!*l)
	break;
      sp[i++] = l;
      if (i == m)
        break;
      while (*l && !(*l == ' ' || *l == '\t'))
	l++;
      if (!*l)
	break;
      *l++ = 0;
    }
  return i;
}

struct deps {
  unsigned int provides;
  unsigned int requires;
  unsigned int obsoletes;
  unsigned int conflicts;
  unsigned int recommends;
  unsigned int supplements;
  unsigned int enhances;
  unsigned int suggests;
  unsigned int freshens;
};

struct parsedata {
  char *kind;
  Source *source;
  char *tmp;
  int tmpl;
};

static Id
makeevr(Pool *pool, char *s)
{
  if (!strncmp(s, "0:", 2) && s[2])
    s += 2;
  return str2id(pool, s, 1);
}

static char *flagtab[] = {
  ">",
  "=",
  ">=",
  "<",
  "!=",
  "<="
};

static char *
join(struct parsedata *pd, char *s1, char *s2, char *s3)
{
  int l = 1;
  char *p;

  if (s1)
    l += strlen(s1);
  if (s2)
    l += strlen(s2);
  if (s3)
    l += strlen(s3);
  if (l > pd->tmpl)
    {
      pd->tmpl = l + 256;
      if (!pd->tmp)
	pd->tmp = malloc(pd->tmpl);
      else
	pd->tmp = realloc(pd->tmp, pd->tmpl);
    }
  p = pd->tmp;
  if (s1)
    {
      strcpy(p, s1);
      p += strlen(s1);
    }
  if (s2)
    {
      strcpy(p, s2);
      p += strlen(s2);
    }
  if (s3)
    {
      strcpy(p, s3);
      p += strlen(s3);
    }
  return pd->tmp;
}

static unsigned int
adddep(Pool *pool, struct parsedata *pd, unsigned int olddeps, char *line, int isreq)
{
  int flags, words;
  Id id, evrid;
  char *sp[4];

  words = 0;
  while (1)
    {
      /* Name [relop evr [rest]] --> 1, 3 or 4 fields.  */
      words += split(line, sp + words, 4 - words);
      if (words == 2)
	{
	  fprintf(stderr, "Bad dependency line: %s\n", line);
	  exit(1);
	}
      line = 0;
      /* Hack, as the content file adds 'package:' for package
         dependencies sometimes.  */
      if (!strncmp (sp[0], "package:", 8))
        sp[0] += 8;
      id = str2id(pool, sp[0], 1);
      if (words >= 3 && strpbrk (sp[1], "<>="))
	{
	  evrid = makeevr(pool, sp[2]);
	  for (flags = 0; flags < 6; flags++)
	    if (!strcmp(sp[1], flagtab[flags]))
	      break;
	  if (flags == 6)
	    {
	      fprintf(stderr, "Unknown relation '%s'\n", sp[1]);
	      exit(1);
	    }
	  id = rel2id(pool, id, evrid, flags + 1, 1);
	  /* Consume three words, there's nothing to move to front.  */
	  if (words == 4)
	    line = sp[3], words = 0;
	}
      else
        {
	  int j;
	  /* Consume one word.  If we had more move them to front.  */
	  words--;
	  for (j = 0; j < words; j++)
	    sp[j] = sp[j+1];
	  if (words == 3)
	    line = sp[2], words = 2;
	}
      olddeps = source_addid_dep(pd->source, olddeps, id, isreq);
      if (!line)
        break;
    }
  return olddeps;
}

Source *
pool_addsource_content(Pool *pool, FILE *fp)
{
  char *line, *linep;
  int aline;
  Source *source;
  Solvable *s;
  struct deps *deps = 0, *dp = 0;
  int pack, i;
  struct parsedata pd;

  source = pool_addsource_empty(pool);
  memset(&pd, 0, sizeof(pd));
  line = malloc(1024);
  aline = 1024;

  pd.source = source;
  linep = line;
  pack = 0;
  s = 0;

  for (;;)
    {
      char *fields[2];
      if (linep - line + 16 > aline)
	{
	  aline = linep - line;
	  line = realloc(line, aline + 512);
	  linep = line + aline;
	  aline += 512;
	}
      if (!fgets(linep, aline - (linep - line), fp))
	break;
      linep += strlen(linep);
      if (linep == line || linep[-1] != '\n')
        continue;
      *--linep = 0;
      linep = line;
      if (split (line, fields, 2) == 2)
        {
	  char *key = fields[0];
	  char *value = fields[1];
	  char *modifier = strchr (key, '.');
	  if (modifier)
	    *modifier++ = 0;
#if 0
	  if (modifier)
	    fprintf (stderr, "key %s, mod %s, value %s\n", key, modifier, fields[1]);
	  else
	    fprintf (stderr, "key %s, value %s\n", key, fields[1]);
#endif

#define istag(x) !strcmp (key, x)
	  if (istag ("PRODUCT"))
	    {
	      /* Only support one product.  */
	      assert (!dp);
	      pd.kind = "product";
	      if ((pack & PACK_BLOCK) == 0)
		{
		  pool->solvables = realloc(pool->solvables, (pool->nsolvables + pack + PACK_BLOCK + 1) * sizeof(Solvable));
		  memset(pool->solvables + source->start + pack, 0, (PACK_BLOCK + 1) * sizeof(Solvable));
		  if (!deps)
		    deps = malloc((pack + PACK_BLOCK + 1) * sizeof(struct deps));
		  else
		    deps = realloc(deps, (pack + PACK_BLOCK + 1) * sizeof(struct deps));
		  memset(deps + pack, 0, (PACK_BLOCK + 1) * sizeof(struct deps));
		}
	      s = pool->solvables + source->start + pack;
	      dp = deps + pack;
	      pack++;
	    }
	  else if (istag ("VERSION"))
	    ;
	  else if (istag ("DISTPRODUCT"))
	    s->name = str2id(pool, join(&pd, pd.kind, ":", value), 1);
	  else if (istag ("DISTVERSION"))
	    s->evr = makeevr(pool, value);
	  else if (istag ("ARCH"))
	    /* Theoretically we want to have the best arch of the given
	       modifiers which still is compatible with the system
	       arch.  We don't know the latter here, though.  */
	    s->arch = str2id (pool, "noarch" , 1);
	  else if (istag ("PREREQUIRES"))
	    dp->requires = adddep(pool, &pd, dp->requires, value, 2);
	  else if (istag ("REQUIRES"))
	    dp->requires = adddep(pool, &pd, dp->requires, value, 1);
	  else if (istag ("PROVIDES"))
	    dp->provides = adddep(pool, &pd, dp->provides, value, 0);
	  else if (istag ("CONFLICTS"))
	    dp->conflicts = adddep(pool, &pd, dp->conflicts, value, 0);
	  else if (istag ("OBSOLETES"))
	    dp->obsoletes = adddep(pool, &pd, dp->obsoletes, value, 0);
	  else if (istag ("RECOMMENDS"))
	    dp->recommends = adddep(pool, &pd, dp->recommends, value, 0);
	  else if (istag ("SUGGESTS"))
	    dp->suggests = adddep(pool, &pd, dp->suggests, value, 0);
	  else if (istag ("SUPPLEMENTS"))
	    dp->supplements = adddep(pool, &pd, dp->supplements, value, 0);
	  else if (istag ("ENHANCES"))
	    dp->enhances = adddep(pool, &pd, dp->enhances, value, 0);
	  /* There doesn't seem to exist FRESHENS.  */
	  /* XXX do something about LINGUAS and ARCH? */
#undef istag
	}
      else
	fprintf (stderr, "malformed line: %s\n", line);
    }

  if (dp && s->arch != ARCH_SRC && s->arch != ARCH_NOSRC)
    dp->provides = source_addid_dep(source, dp->provides, rel2id(pool, s->name, s->evr, REL_EQ, 1), 0);
  if (dp)
    dp->supplements = source_fix_legacy(source, dp->provides, dp->supplements);
    
  pool->nsolvables += pack;
  source->nsolvables = pack;
  s = pool->solvables + source->start;
  for (i = 0; i < pack; i++, s++)
    {
      if (deps[i].provides)
        s->provides = source->idarraydata + deps[i].provides;
      if (deps[i].requires)
        s->requires = source->idarraydata + deps[i].requires;
      if (deps[i].conflicts)
        s->conflicts = source->idarraydata + deps[i].conflicts;
      if (deps[i].obsoletes)
        s->obsoletes = source->idarraydata + deps[i].obsoletes;
      if (deps[i].recommends)
        s->recommends = source->idarraydata + deps[i].recommends;
      if (deps[i].supplements)
        s->supplements = source->idarraydata + deps[i].supplements;
      if (deps[i].suggests)
        s->suggests = source->idarraydata + deps[i].suggests;
      if (deps[i].enhances)
        s->enhances = source->idarraydata + deps[i].enhances;
      if (deps[i].freshens)
        s->freshens = source->idarraydata + deps[i].freshens;
    }

  free(deps);
  if (pd.tmp)
    free(pd.tmp);
  free(line);

  return source;
}
