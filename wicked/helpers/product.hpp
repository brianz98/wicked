#ifndef _wicked_product_h_
#define _wicked_product_h_

#include <vector>

template <class T> class Product {
  using prod_t = std::vector<T>;

public:
  Product() {}
  Product(const prod_t &elements) : elements_(elements) {}
  Product(std::initializer_list<T> elements) : elements_(elements) {}

  size_t size() const { return elements_.size(); }

  void push_back(const T &e) { elements_.push_back(e); }

  bool operator<(const Product<T> &other) const {
    return elements_ < other.elements_;
  }

  T &operator[](size_t n) { return elements_[n]; }

  const T &operator[](size_t n) const { return elements_[n]; }

  typename std::vector<T>::iterator begin() { return elements_.begin(); }
  typename std::vector<T>::const_iterator begin() const {
    return elements_.begin();
  }

  typename std::vector<T>::iterator end() { return elements_.end(); }
  typename std::vector<T>::const_iterator end() const {
    return elements_.end();
  }

protected:
  prod_t elements_;
};

#endif // _wicked_product_h_