#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
//
#include "url.h"
#include "bool.h"
#include "urlconnection.h"
#include "streamtokenizer.h"
#include "html-utils.h"
#include "hashset.h"
#include "hashsets-functions.h"

static void Welcome(const char *welcomeTextFileName);
static void BuildIndices(hashset *index, hashset *seenArticles, hashset *stopWords);
static void ProcessFeed(const char *remoteDocumentName, hashset *index, hashset *seenArticles, hashset *stopWords);
static void PullAllNewsItems(urlconnection *urlconn, hashset *index, hashset *seenArticles, hashset *stopWords);
static bool GetNextItemTag(streamtokenizer *st);
static void ProcessSingleNewsItem(streamtokenizer *st, hashset *index, hashset *seenArticles, hashset *stopWords);
static void ExtractElement(streamtokenizer *st, const char *htmlTag, char dataBuffer[], int bufferLength);
static void ParseArticle(const char *articleTitle, const char *articleDescription, const char *articleURL, 
			 hashset *index, hashset *seenArticles, hashset *stopWords);
static void ScanArticle(streamtokenizer *st, const char *articleTitle, const char *serverName, 
			const char *articleURL, hashset *index, hashset *stopWords);
static void ProcessWord(const char *word, const char *articleTitle, const char *articleURL, 
			const char *serverName, hashset *index);
static void ProcessArticle(struct wordArticles *wordArt, void *found, struct article *art, hashset *index);
static void QueryIndices(hashset *stopWords, hashset *index);
static void ProcessResponse(const char *word, hashset *stopWords, hashset *index);
static void GetResults(const char *word, hashset *index);
static void PrintResults(vector *results, int n);
static bool WordIsWellFormed(const char *word);
void AddStopWords(hashset *stopWords);
static bool IsStopWord(hashset *stopWords, const char *word);


static const char *const kWelcomeTextFile = "/home/suvov/CS107/A4/assn-4-rss-news-search-data/welcome.txt";
static const char *const kDefaultFeedsFile = "/home/suvov/CS107/A4/assn-4-rss-news-search-data/rss-feeds.txt";
static const char *const kDefaultStopWordsFile = "/home/suvov/CS107/A4/assn-4-rss-news-search-data/stop-words.txt";
static const char *const kNewLineDelimiters = "\r\n";

//from high to low cmp func for article struct
static int CompareByOccur(const void *elemAddr1, const void *elemAddr2)
{
  struct article *art1 = *(struct article **) elemAddr1;
  struct article *art2 = *(struct article **) elemAddr2;
  if(art1->occurrences < art2->occurrences) return 1;
  else if(art1->occurrences  > art2->occurrences) return -1;
  return 0;
}



int main(int argc, char **argv)
{
  hashset index;
  hashset seenArticles;
  hashset stopWords;
  AddStopWords(&stopWords);
  HashSetNew(&seenArticles, sizeof(struct article *), 1009, ArticleHash, ArticleCmp, FreeArticle);
  HashSetNew(&index, sizeof(struct wordArticles *), 1009, IndexHash, IndexCmp, FreeIndex);
  Welcome(kWelcomeTextFile);
  BuildIndices(&index, &seenArticles, &stopWords);
  QueryIndices(&stopWords, &index);//
  HashSetDispose(&index);
  HashSetDispose(&seenArticles);
  HashSetDispose(&stopWords);
  return 0;
}



// Adds stop-words, read from a file to hashset
void AddStopWords(hashset *stopWords)
{
  HashSetNew(stopWords, sizeof(char *), 1009, StringHash, StrCmp, FreeString); 
  FILE *infile;
  infile = fopen(kDefaultStopWordsFile, "r");
  assert(infile != NULL);
  streamtokenizer st;
  char word[128];
  STNew(&st, infile, kNewLineDelimiters, true);
  while(STNextToken(&st, word, sizeof(word))) { 
    char *copy = strdup(word);
    HashSetEnter(stopWords, &copy);
  }
  STDispose(&st);
  fclose(infile);
}

/** 
 * Function: Welcome
 * -----------------
 * Displays the contents of the specified file, which
 * holds the introductory remarks to be printed every time
 * the application launches.  This type of overhead may
 * seem silly, but by placing the text in an external file,
 * we can change the welcome text without forcing a recompilation and
 * build of the application.  It's as if welcomeTextFileName
 * is a configuration file that travels with the application.
 */
 

static void Welcome(const char *welcomeTextFileName)
{
  FILE *infile;
  streamtokenizer st;
  char buffer[1024];
  
  infile = fopen(welcomeTextFileName, "r");
  assert(infile != NULL);    
  
  STNew(&st, infile, kNewLineDelimiters, true);
  while (STNextToken(&st, buffer, sizeof(buffer))) {
    printf("%s\n", buffer);
  }
  
  printf("\n");
  STDispose(&st); // remember that STDispose doesn't close the file, since STNew doesn't open one.. 
  fclose(infile);
}

/**
 * Function: BuildIndices
 * ----------------------
 * As far as the user is concerned, BuildIndices needs to read each and every
 * one of the feeds listed in the specied feedsFileName, and for each feed parse
 * content of all referenced articles and store the content in the hashset of indices.
 * Each line of the specified feeds file looks like this:
 *
 *   <feed name>: <URL of remore xml document>
 *
 * Each iteration of the supplied while loop parses and discards the feed name (it's
 * in the file for humans to read, but our aggregator doesn't care what the name is)
 * and then extracts the URL.  It then relies on ProcessFeed to pull the remote
 * document and index its content.
 */

static void BuildIndices(hashset *index, hashset *seenArticles, hashset *stopWords)
{
  FILE *infile;
  streamtokenizer st;
  char remoteFileName[1024];
  
  infile = fopen(kDefaultFeedsFile, "r");
  assert(infile != NULL);
  STNew(&st, infile, kNewLineDelimiters, true);
  while (STSkipUntil(&st, ":") != EOF) { // ignore everything up to the first semicolon of the line
    STSkipOver(&st, ": ");		 // now ignore the semicolon and any whitespace directly after it
    STNextToken(&st, remoteFileName, sizeof(remoteFileName));   
    ProcessFeed(remoteFileName, index, seenArticles, stopWords);
  }
  
  STDispose(&st);
  fclose(infile);
  printf("\n");
}


/**
 * Function: ProcessFeed
 * ---------------------
 * ProcessFeed locates the specified RSS document, and if a (possibly redirected) connection to that remote
 * document can be established, then PullAllNewsItems is tapped to actually read the feed.  Check out the
 * documentation of the PullAllNewsItems function for more information, and inspect the documentation
 * for ParseArticle for information about what the different response codes mean.
 */

static void ProcessFeed(const char *remoteDocumentName, hashset *index, hashset *seenArticles, hashset *stopWords)
{
  url u;
  urlconnection urlconn;
  
  URLNewAbsolute(&u, remoteDocumentName);
  URLConnectionNew(&urlconn, &u);
  
  switch (urlconn.responseCode) {
      case 0: printf("Unable to connect to \"%s\".  Ignoring...", u.serverName);
              break;
  case 200: PullAllNewsItems(&urlconn, index, seenArticles, stopWords);
                break;
      case 301: 
  case 302: ProcessFeed(urlconn.newUrl, index, seenArticles, stopWords);
                break;
      default: printf("Connection to \"%s\" was established, but unable to retrieve \"%s\". [response code: %d, response message:\"%s\"]\n",
		      u.serverName, u.fileName, urlconn.responseCode, urlconn.responseMessage);
	       break;
  };
  
  URLConnectionDispose(&urlconn);
  URLDispose(&u);
}

/**
 * Function: PullAllNewsItems
 * --------------------------
 * Steps though the data of what is assumed to be an RSS feed identifying the names and
 * URLs of online news articles.  Check out "datafiles/sample-rss-feed.txt" for an idea of what an
 * RSS feed from the www.nytimes.com (or anything other server that syndicates is stories).
 *
 * PullAllNewsItems views a typical RSS feed as a sequence of "items", where each item is detailed
 * using a generalization of HTML called XML.  A typical XML fragment for a single news item will certainly
 * adhere to the format of the following example:
 *
 * <item>
 *   <title>At Installation Mass, New Pope Strikes a Tone of Openness</title>
 *   <link>http://www.nytimes.com/2005/04/24/international/worldspecial2/24cnd-pope.html</link>
 *   <description>The Mass, which drew 350,000 spectators, marked an important moment in the transformation of Benedict XVI.</description>
 *   <author>By IAN FISHER and LAURIE GOODSTEIN</author>
 *   <pubDate>Sun, 24 Apr 2005 00:00:00 EDT</pubDate>
 *   <guid isPermaLink="false">http://www.nytimes.com/2005/04/24/international/worldspecial2/24cnd-pope.html</guid>
 * </item>
 *
 * PullAllNewsItems reads and discards all characters up through the opening <item> tag (discarding the <item> tag
 * as well, because once it's read and indentified, it's been pulled,) and then hands the state of the stream to
 * ProcessSingleNewsItem, which handles the job of pulling and analyzing everything up through and including the </item>
 * tag. PullAllNewsItems processes the entire RSS feed and repeatedly advancing to the next <item> tag and then allowing
 * ProcessSingleNewsItem do process everything up until </item>.
 */

static const char *const kTextDelimiters = " \t\n\r\b!@$%^*()_+={[}]|\\'\":;/?.>,<~`";
static void PullAllNewsItems(urlconnection *urlconn, hashset *index, hashset *seenArticles, hashset *stopWords)
{
  streamtokenizer st;
  STNew(&st, urlconn->dataStream, kTextDelimiters, false);
  while (GetNextItemTag(&st)) { // if true is returned, then assume that <item ...> has just been read and pulled from the data stream
    ProcessSingleNewsItem(&st, index, seenArticles, stopWords);
  }
  
  STDispose(&st);
}

/**
 * Function: GetNextItemTag
 * ------------------------
 * Works more or less like GetNextTag below, but this time
 * we're searching for an <item> tag, since that marks the
 * beginning of a block of HTML that's relevant to us.  
 * 
 * Note that each tag is compared to "<item" and not "<item>".
 * That's because the item tag, though unlikely, could include
 * attributes and perhaps look like any one of these:
 *
 *   <item>
 *   <item rdf:about="Latin America reacts to the Vatican">
 *   <item requiresPassword=true>
 *
 * We're just trying to be as general as possible without
 * going overboard.  (Note that we use strncasecmp so that
 * string comparisons are case-insensitive.  That's the case
 * throughout the entire code base.)
 */

static const char *const kItemTagPrefix = "<item";
static bool GetNextItemTag(streamtokenizer *st)
{
  char htmlTag[1024];
  while (GetNextTag(st, htmlTag, sizeof(htmlTag))) {
    if (strncasecmp(htmlTag, kItemTagPrefix, strlen(kItemTagPrefix)) == 0) {
      return true;
    }
  }	 
  return false;
}

/**
 * Function: ProcessSingleNewsItem
 * -------------------------------
 * Code which parses the contents of a single <item> node within an RSS/XML feed.
 * At the moment this function is called, we're to assume that the <item> tag was just
 * read and that the streamtokenizer is currently pointing to everything else, as with:
 *   
 *      <title>Carrie Underwood takes American Idol Crown</title>
 *      <description>Oklahoma farm girl beats out Alabama rocker Bo Bice and 100,000 other contestants to win competition.</description>
 *      <link>http://www.nytimes.com/frontpagenews/2841028302.html</link>
 *   </item>
 *
 * ProcessSingleNewsItem parses everything up through and including the </item>, storing the title, link, and article
 * description in local buffers long enough so that the online new article identified by the link can itself be parsed
 * and indexed.  We don't rely on <title>, <link>, and <description> coming in any particular order.  We do asssume that
 * the link field exists (although we can certainly proceed if the title and article descrption are missing.)  There
 * are often other tags inside an item, but we ignore them.
 */

static const char *const kItemEndTag = "</item>";
static const char *const kTitleTagPrefix = "<title";
static const char *const kDescriptionTagPrefix = "<description";
static const char *const kLinkTagPrefix = "<link";
static void ProcessSingleNewsItem(streamtokenizer *st, hashset *index, hashset *seenArticles, hashset *stopWords)
{
  char htmlTag[1024];
  char articleTitle[1024];
  char articleDescription[1024];
  char articleURL[1024];
  articleTitle[0] = articleDescription[0] = articleURL[0] = '\0';
  
  while (GetNextTag(st, htmlTag, sizeof(htmlTag)) && (strcasecmp(htmlTag, kItemEndTag) != 0)) {
    if (strncasecmp(htmlTag, kTitleTagPrefix, strlen(kTitleTagPrefix)) == 0) ExtractElement(st, htmlTag, articleTitle, sizeof(articleTitle));
    if (strncasecmp(htmlTag, kDescriptionTagPrefix, strlen(kDescriptionTagPrefix)) == 0) ExtractElement(st, htmlTag, articleDescription, sizeof(articleDescription));
    if (strncasecmp(htmlTag, kLinkTagPrefix, strlen(kLinkTagPrefix)) == 0) ExtractElement(st, htmlTag, articleURL, sizeof(articleURL));
  }
  
  if (strncmp(articleURL, "", sizeof(articleURL)) == 0) return;     // punt, since it's not going to take us anywhere
  ParseArticle(articleTitle, articleDescription, articleURL, index, seenArticles, stopWords);
}

/**
 * Function: ExtractElement
 * ------------------------
 * Potentially pulls text from the stream up through and including the matching end tag.  It assumes that
 * the most recently extracted HTML tag resides in the buffer addressed by htmlTag.  The implementation
 * populates the specified data buffer with all of the text up to but not including the opening '<' of the
 * closing tag, and then skips over all of the closing tag as irrelevant.  Assuming for illustration purposes
 * that htmlTag addresses a buffer containing "<description" followed by other text, these three scanarios are
 * handled:
 *
 *    Normal Situation:     <description>http://some.server.com/someRelativePath.html</description>
 *    Uncommon Situation:   <description></description>
 *    Uncommon Situation:   <description/>
 *
 * In each of the second and third scenarios, the document has omitted the data.  This is not uncommon
 * for the description data to be missing, so we need to cover all three scenarious (I've actually seen all three.)
 * It would be quite unusual for the title and/or link fields to be empty, but this handles those possibilities too.
 */
 
static void ExtractElement(streamtokenizer *st, const char *htmlTag, char dataBuffer[], int bufferLength)
{
  assert(htmlTag[strlen(htmlTag) - 1] == '>');
  if (htmlTag[strlen(htmlTag) - 2] == '/') return;    // e.g. <description/> would state that a description is not being supplied
  STNextTokenUsingDifferentDelimiters(st, dataBuffer, bufferLength, "<");
  RemoveEscapeCharacters(dataBuffer);
  if (dataBuffer[0] == '<') strcpy(dataBuffer, "");  // e.g. <description></description> also means there's no description
  STSkipUntil(st, ">");
  STSkipOver(st, ">");
}

/** 
 * Function: ParseArticle
 * ----------------------
 * Attempts to establish a network connect to the news article identified by the three
 * parameters.  The network connection is either established of not.  The implementation
 * is prepared to handle a subset of possible (but by far the most common) scenarios,
 * and those scenarios are categorized by response code:
 *
 *    0 means that the server in the URL doesn't even exist or couldn't be contacted.
 *    200 means that the document exists and that a connection to that very document has
 *        been established.
 *    301 means that the document has moved to a new location
 *    302 also means that the document has moved to a new location
 *    4xx and 5xx (which are covered by the default case) means that either
 *        we didn't have access to the document (403), the document didn't exist (404),
 *        or that the server failed in some undocumented way (5xx).
 *
 * The are other response codes, but for the time being we're punting on them, since
 * no others appears all that often, and it'd be tedious to be fully exhaustive in our
 * enumeration of all possibilities.
 */

static void ParseArticle(const char *articleTitle, const char *articleDescription, const char *articleURL, 
			 hashset *index, hashset *seenArticles, hashset *stopWords)
{
  url u;
  urlconnection urlconn;
  streamtokenizer st;

  URLNewAbsolute(&u, articleURL);
  URLConnectionNew(&urlconn, &u);
  
  switch (urlconn.responseCode) {
      case 0: printf("Unable to connect to \"%s\".  Domain name or IP address is nonexistent.\n", articleURL);
	      break;
      case 200: printf("Scanning \"%s\" from \"http://%s\"\n", articleTitle, u.serverName);
	        STNew(&st, urlconn.dataStream, kTextDelimiters, false);
		struct article *art = malloc(sizeof(struct article));
		strcpy(art->title, articleTitle);
		strcpy(art->URL, articleURL);
		strcpy(art->server, u.serverName);
		void *found = HashSetLookup(seenArticles, &art);
		if(found == NULL && strlen(articleTitle) > 0) {
		  HashSetEnter(seenArticles, &art);
		  ScanArticle(&st, articleTitle, u.serverName, articleURL, index, stopWords);
		}
		STDispose(&st);
		break;
      case 301:
      case 302: // just pretend we have the redirected URL all along, though index using the new URL and not the old one...
	ParseArticle(articleTitle, articleDescription, urlconn.newUrl, index, seenArticles, stopWords);
		break;
      default: printf("Unable to pull \"%s\" from \"%s\". [Response code: %d] Punting...\n", articleTitle, u.serverName, urlconn.responseCode);
	       break;
  }
  
  URLConnectionDispose(&urlconn);
  URLDispose(&u);
}

/**
 * Function: ScanArticle
 * ---------------------
 * Parses the specified article, skipping over all HTML tags, and counts the numbers
 * of well-formed words that could potentially serve as keys in the set of indices.
 * Once the full article has been scanned, the number of well-formed words is
 * printed, and the longest well-formed word we encountered along the way
 * is printed as well.
 *
 * This is really a placeholder implementation for what will ultimately be
 * code that indexes the specified content.
 */

static void ScanArticle(streamtokenizer *st, const char *articleTitle, const char *serverName, 
			const char *articleURL, hashset *index, hashset *stopWords)
{
  char word[1024];
  while (STNextToken(st, word, sizeof(word))) {
    if (strcasecmp(word, "<") == 0) {
      SkipIrrelevantContent(st); // in html-utls.h
    } else {
      RemoveEscapeCharacters(word);
      if (WordIsWellFormed(word) && !IsStopWord(stopWords, word)) {
	ProcessWord(word, articleTitle, articleURL, serverName, index);
      }
    }
  }
    printf("\n");
}

//
static void ProcessWord(const char *word, const char *articleTitle, const char *articleURL, 
			const char *serverName, hashset *index)
{ 
  struct wordArticles *wordArt = malloc(sizeof(struct wordArticles));
  strcpy(wordArt->word, word);
  struct article *art = malloc(sizeof(struct article));
  strcpy(art->title, articleTitle);
  strcpy(art->URL, articleURL);
  strcpy(art->server, serverName);
  art->occurrences = 1;
  void *found = HashSetLookup(index, &wordArt); // compares only by word, so it's enough
  if(found == NULL) { // word seen first time
    VectorNew(&wordArt->articles, sizeof(struct article *), FreeArticle, 0);
    VectorAppend(&wordArt->articles, &art);
    HashSetEnter(index, &wordArt);
  }else{ // word already in index
    ProcessArticle(wordArt, found, art, index);
  }  
}

//
static void ProcessArticle(struct wordArticles *wordArt, void *found, struct article *art, hashset *index)
{ 
  free(wordArt); // don't need that memory anymore
  wordArt = *(struct wordArticles **) found; 
  int vfound = VectorSearch(&wordArt->articles, &art, ArticleCmp, 0, false); // look if article already in the vector
  if(vfound == -1) {
    VectorAppend(&wordArt->articles, &art); // if given article isn't in vector of word's articles, append it
  }else{
    free(art); // don't need this memory
    art = *(struct article **)VectorNth(&wordArt->articles, vfound);
    art->occurrences++; // if article is already in vector, increment it occurrences value
  }
}

/** 
 * Function: QueryIndices
 * ----------------------
 * Standard query loop that allows the user to specify a single search term, and
 * then proceeds (via ProcessResponse) to list up to 10 articles (sorted by relevance)
 * that contain that word.
 */

static void QueryIndices(hashset *stopWords, hashset *index)
{
  char response[1024];
  while (true) {
    printf("Please enter a single query term that might be in our set of indices [enter to quit]: ");
    fgets(response, sizeof(response), stdin);
    response[strlen(response) - 1] = '\0';
    if (strcasecmp(response, "") == 0) break;
    ProcessResponse(response, stopWords, index);
    printf("\n");
  }
}

/** 
 * Function: ProcessResponse
 * -------------------------
 * Placeholder implementation for what will become the search of a set of indices
 * for a list of web documents containing the specified word.
 */

static void ProcessResponse(const char *word, hashset *stopWords, hashset *index)
{
  if (WordIsWellFormed(word)) {
    if(IsStopWord(stopWords, word)) {
      printf("Too common a word to be taken seriously. Try something more specific.\n");
      return; // break out 
    }else{
      GetResults(word, index);
    } 
  } else {
    printf("\tWe won't be allowing words like \"%s\" into our set of indices.\n", word);
  }
}

//
static void GetResults(const char *word, hashset *index)
{
  struct wordArticles *wordArt = malloc(sizeof(struct wordArticles));
  strcpy(wordArt->word, word);
  void *found = HashSetLookup(index, &wordArt);
  free(wordArt); // silly, but it's the easiest way to look up a word in index table
  if(found == NULL) {
    printf("None of today's news articles contain the word \"%s\" \n", word);
  }else{
    wordArt = *(struct wordArticles **) found;
    vector *results = &wordArt->articles;
    VectorSort(results, CompareByOccur);
    int n = VectorLength(results);
    printf("Nice! We found \"%d\" articles that include the word: \"%s\". \n", n, word); 
    PrintResults(results, n);
  } 
}

//
static void PrintResults(vector *results, int n)
{
  struct article *art;
  for(int i = 0; i < n; i++) {
    int index = i+1;
    art = *(struct article **)VectorNth(results, i);
    printf("%d.) \"%s\" [search term occurs %d times]\n\"%s\"\n", index, art->title, art->occurrences, art->URL);
  }
}

//
static bool WordIsWellFormed(const char *word)
{
  int i;
  if (strlen(word) == 0) return true;
  if (!isalpha((int) word[0])) return false;
  for (i = 1; i < strlen(word); i++)
    if (!isalnum((int) word[i]) && (word[i] != '-')) return false; 

  return true;
}

//
static bool IsStopWord(hashset *stopWords, const char *word)
{
  char *dummy = strdup(word);
  void *result = HashSetLookup(stopWords, &dummy);
  if(result != NULL) return true;
  return false;
}
