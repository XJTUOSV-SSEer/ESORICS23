import hashlib

word_freq = {
    "2001": 100000
}

with open("sse_data_test", "w") as f:
    f.write(str(len(word_freq.keys())) + "\n")
    for _k in word_freq.keys():
        f.write(_k + "\n")
        f.write(str(word_freq[_k]) + "\n")
        for i in range(word_freq[_k]):
            f.write(hashlib.sha256((_k+str(i)).encode()).hexdigest()[:5] + "\n")

