#pragma once
#include <unordered_set>
#include <string>
#include <string_view>

class StopWords {
public:
    static const std::unordered_set<std::string>& get() {
        static const std::unordered_set<std::string> instance = build();
        return instance;
    }

    static bool isStopWord(std::string_view word) {
        return get().find(std::string(word)) != get().end();
    }

private:
    static std::unordered_set<std::string> build() {
        return {
            "a", "an", "the", "and", "or", "but", "for", "nor", "on", "at", "to", "by",
            "in", "of", "off", "with", "without", "via", "per", "among", "between",
            "above", "below", "up", "down", "over", "under", "further", "then", "now",
            "so", "than", "that", "these", "those", "this", "thus", "hence", "according",
            "accordingly", "across", "after", "afterwards", "again", "against",
            "all", "almost", "alone", "along", "already", "also", "although",
            "always", "am", "amongst", "amount", "an", "another", "any", "anyhow",
            "anyone", "anything", "anyway", "anywhere", "are", "around", "as",
            "at", "back", "be", "became", "because", "become", "becomes", "becoming",
            "been", "before", "beforehand", "behind", "being", "below", "beside",
            "besides", "between", "beyond", "both", "bottom", "but", "by", "call",
            "can", "cannot", "can't", "co", "con", "could", "couldn't", "de", "do",
            "does", "doesn't", "doing", "don't", "done", "down", "due", "during",
            "each", "eight", "either", "eleven", "else", "elsewhere", "empty",
            "enough", "even", "ever", "every", "everyone", "everything", "everywhere",
            "except", "few", "fifteen", "fifty", "first", "five", "for", "former",
            "formerly", "forty", "four", "from", "front", "full", "further", "get",
            "give", "go", "had", "has", "hasn't", "have", "haven't", "having", "he",
            "hence", "her", "here", "hereafter", "hereby", "herein", "hereupon",
            "hers", "herself", "him", "himself", "his", "how", "however", "hundred",
            "i", "if", "in", "indeed", "into", "is", "isn't", "it", "its", "itself",
            "just", "keep", "last", "latter", "latterly", "least", "less", "made",
            "make", "many", "may", "me", "meanwhile", "might", "mill", "mine",
            "more", "moreover", "most", "mostly", "move", "much", "must", "my",
            "myself", "name", "namely", "neither", "never", "nevertheless", "next",
            "nine", "nobody", "none", "noone", "nor", "not", "nothing", "now",
            "nowhere", "of", "off", "often", "on", "once", "one", "only", "onto",
            "or", "other", "others", "otherwise", "our", "ours", "ourselves", "out",
            "over", "own", "part", "per", "perhaps", "please", "put", "rather",
            "re", "same", "see", "seem", "seemed", "seeming", "seems", "serious",
            "several", "she", "should", "shouldn't", "show", "side", "since",
            "sincere", "six", "sixty", "so", "some", "somehow", "someone", "something",
            "sometime", "sometimes", "somewhere", "still", "such", "take", "ten",
            "than", "that", "the", "their", "them", "themselves", "then", "thence",
            "there", "thereafter", "thereby", "therefore", "therein", "thereupon",
            "these", "they", "thick", "thin", "third", "this", "those", "though",
            "three", "through", "throughout", "thru", "thus", "to", "together",
            "too", "top", "toward", "towards", "twelve", "twenty", "two", "un",
            "under", "unless", "until", "up", "upon", "us", "very", "via", "was",
            "wasn't", "we", "well", "were", "weren't", "what", "whatever", "when",
            "whence", "whenever", "where", "whereafter", "whereas", "whereby",
            "wherein", "whereupon", "wherever", "whether", "which", "while",
            "whither", "who", "whoever", "whole", "whom", "whose", "why", "will",
            "with", "within", "without", "would", "wouldn't", "yet", "you", "your",
            "yours", "yourself", "yourselves"
        };
    }
};