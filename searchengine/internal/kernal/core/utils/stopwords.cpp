#include "stopwords.hpp"
#include <unordered_set>
#include <iostream>

namespace StopWords {

    // Build the stop‑word set (initialised once at program start)
    static const std::unordered_set<std::string, ci_hash, ci_equal> stopSet = {
        // Common English stop words (from NLTK + extras)
        "a", "about", "above", "across", "after", "afterwards", "again", "against",
        "all", "almost", "alone", "along", "already", "also", "although", "always",
        "am", "among", "amongst", "amount", "an", "and", "another", "any", "anyhow",
        "anyone", "anything", "anyway", "anywhere", "are", "around", "as", "at",
        "back", "be", "became", "because", "become", "becomes", "becoming", "been",
        "before", "beforehand", "behind", "being", "below", "beside", "besides",
        "between", "beyond", "both", "bottom", "but", "by", "call", "can", "cannot",
        "could", "did", "do", "does", "doing", "done", "down", "due", "during",
        "each", "eight", "either", "else", "elsewhere", "empty", "enough", "even",
        "ever", "every", "everyone", "everything", "everywhere", "except", "few",
        "fifteen", "fifty", "first", "five", "for", "former", "formerly", "forty",
        "four", "from", "front", "full", "further", "get", "give", "go", "had",
        "has", "have", "he", "hence", "her", "here", "hereafter", "hereby",
        "herein", "hereupon", "hers", "herself", "him", "himself", "his", "how",
        "however", "hundred", "i", "if", "in", "indeed", "into", "is", "it",
        "its", "itself", "just", "keep", "last", "later", "latter", "latterly",
        "least", "less", "let", "like", "likely", "ltd", "made", "make", "many",
        "may", "me", "meanwhile", "might", "mine", "more", "moreover", "most",
        "mostly", "much", "must", "my", "myself", "name", "namely", "neither",
        "never", "nevertheless", "next", "nine", "no", "nobody", "none", "noone",
        "nor", "not", "nothing", "now", "nowhere", "of", "off", "often", "on",
        "once", "one", "only", "onto", "or", "other", "others", "otherwise", "our",
        "ours", "ourselves", "out", "over", "own", "part", "per", "perhaps",
        "please", "put", "rather", "re", "same", "see", "seem", "seemed",
        "seeming", "seems", "serious", "several", "she", "should", "show", "side",
        "since", "sincere", "six", "sixty", "so", "some", "somehow", "someone",
        "something", "sometime", "sometimes", "somewhere", "still", "such", "take",
        "ten", "than", "that", "the", "their", "them", "themselves", "then",
        "thence", "there", "thereafter", "thereby", "therefore", "therein",
        "thereupon", "these", "they", "thick", "thin", "third", "this", "those",
        "though", "three", "through", "throughout", "thru", "thus", "to", "together",
        "too", "top", "toward", "towards", "twelve", "twenty", "two", "under",
        "until", "up", "upon", "us", "very", "via", "was", "we", "well", "were",
        "what", "whatever", "when", "whence", "whenever", "where", "whereafter",
        "whereas", "whereby", "wherein", "whereupon", "wherever", "whether",
        "which", "while", "whither", "who", "whoever", "whole", "whom", "whose",
        "why", "will", "with", "within", "without", "would", "yet", "you", "your",
        "yours", "yourself", "yourselves"
    };

    bool isStopWord(std::string_view word) noexcept {
        if (word.empty()) return false;
        return stopSet.find(word) != stopSet.end();
    }

}
