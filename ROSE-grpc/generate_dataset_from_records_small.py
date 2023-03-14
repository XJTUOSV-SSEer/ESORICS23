import hashlib

word_freq = {
    "2001": 24,
    "pst": 21,
    "2000": 20,
    "call": 10,
    "thu": 93,
    "question": 83,
    "follow": 75,
    "regard": 68,
    "contact": 60,
    "energi": 54,
    "current": 47,
    "legal": 39,
    "problem": 31,
    "industri": 21,
    "transport": 12,
    "target": 7,
    "exactli": 4,
    "enterpris": 3
}

with open("sse_data_test", "w") as f:
    f.write(str(len(word_freq.keys())) + "\n")
    for _k in word_freq.keys():
        f.write(_k + "\n")
        f.write(str(word_freq[_k]) + "\n")
        for i in range(word_freq[_k]):
            f.write(hashlib.sha256((_k+str(i)).encode()).hexdigest()[:5] + "\n")

