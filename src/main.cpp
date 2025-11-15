template<typename T>
class UniquePtr {
    public:
        T* data;
    explicit UniquePtr(T* data) : data(data) {}
    ~UniquePtr() {
        delete data;
    }
    T* get() const {
        return data;
    }
    T& operator*() {
        return *data;
    }
    T* operator->() {
        return data;
    }

    UniquePtr& operator=(const UniquePtr& rhs) = delete;
    UniquePtr(const UniquePtr&) = delete;

};