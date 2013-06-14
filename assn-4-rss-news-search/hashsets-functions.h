#ifndef __hashsets_functions_
#define __hashsets_functions_

typedef struct article {
  char title[1024];
  char URL[1024];
  char server[1024];
  int occurrences; //could've used short here but it anyway will be rounded off to be the multiple of 4
} article;

typedef struct wordArticles {
  char word[1024];
  vector articles; // vector of articles
} wordArticles;


/******Stop-words functions*/

// Hash function
static const signed long kHashMultiplier = -1664117991L;
static int StringHash(const void *elemAddr, int numBuckets)  
{      
  int i;
  unsigned long hashcode = 0;
  char * svalue = *(char **) elemAddr; // actually it just passes address which we have to interpret
  for (i = 0; i < strlen(svalue); i++)  
    hashcode = hashcode * kHashMultiplier + tolower(svalue[i]);  
  
    return hashcode % numBuckets;                            
}

// Free Function
static void FreeString(void *elemAddr)
{
  char *s = *(char **) elemAddr;
  free(s);
}

// Compare Function
static int StrCmp(const void *elemAddr1, const void *elemAddr2) 
{
  const char *s1 = *(char **) elemAddr1;
  const char *s2 = *(char **) elemAddr2;

  return strcasecmp(s1, s2);
}

// Map Function
static void PrintStr(void *elemAddr, void *auxData)
{
  char *s = *(char **) elemAddr;
  printf("%s\n", s);
}
/******end of stop-words hashset functions*/

/******SeenArticles functions*/

// Hash function
static int ArticleHash(const void *elemAddr, int numBuckets)  
{      
  int i;
  unsigned long hashcode = 0;
  struct article *art = *(struct article **) elemAddr;
  char *svalue = art->URL;
  for (i = 0; i < strlen(svalue); i++)  
    hashcode = hashcode * kHashMultiplier + tolower(svalue[i]);  
  
    return hashcode % numBuckets;                            
}

// Free Function
static void FreeArticle(void *elemAddr)
{
  struct article *art = *(struct article **) elemAddr;
  free(art);
}

// Compare Function
static int ArticleCmp(const void *elemAddr1, const void *elemAddr2) 
{
  struct article *art1 = *(struct article **) elemAddr1;
  struct article *art2 = *(struct article **) elemAddr2;
  if(strcmp(art1->URL, art2->URL) == 0) return 0;
  else if(strcmp(art1->title, art2->title) == 0 && strcmp(art1->server, art2->server) == 0) return 0;
  else return -1; 
	
}

// Map Function
static void PrintArticle(void *elemAddr, void *auxData)
{
}
/******end of seenArticle functions*/


/******Index functions*/

// Hash function
static int IndexHash(const void *elemAddr, int numBuckets)  
{      
  int i;
  unsigned long hashcode = 0;
  struct wordArticles *wordArt = *(struct wordArticles **) elemAddr;
  
  char *svalue = wordArt->word;
  for (i = 0; i < strlen(svalue); i++)  
    hashcode = hashcode * kHashMultiplier + tolower(svalue[i]);  
  
    return hashcode % numBuckets;                            
}

// Free Function
static void FreeIndex(void *elemAddr)
{
  struct wordArticles *wordArt = *(struct wordArticles **) elemAddr;
  VectorDispose(&wordArt->articles);
  free(wordArt);
}

// Compare Function
static int IndexCmp(const void *elemAddr1, const void *elemAddr2) 
{
  struct wordArticles *wordArt1 = *(struct wordArticles **) elemAddr1;
  struct wordArticles *wordArt2 = *(struct wordArticles **) elemAddr2;
  
  return strcasecmp(wordArt1->word, wordArt2->word);
	
}

// Map Function
static void PrintIndex(void *elemAddr, void *auxData)
{
  struct wordArticles *wordArt = *(struct wordArticles **) elemAddr;
  printf("For word \"%s\" there are \"%d\" articles \n", wordArt->word, VectorLength(&wordArt->articles));
}
/******end of Index functions*/











#endif
