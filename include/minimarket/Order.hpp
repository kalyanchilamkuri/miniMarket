#pragma once

#include "minimarket/Types.hpp"
#include <string>

namespace minimarket {

class Order {
public:
    Order(OrderId id,
          std::string client_id,
          Side side,
          OrderType type,
          Price price,
          Quantity qty,
          Timestamp timestamp)
        : id_(id),
          client_id_(std::move(client_id)),
          side_(side),
          type_(type),
          price_(price),
          total_qty_(qty),
          open_qty_(qty),
          timestamp_(timestamp) {}

    OrderId id() const { return id_; }
    const std::string& clientId() const { return client_id_; }
    Side side() const { return side_; }
    OrderType type() const { return type_; }
    Price price() const { return price_; }
    Quantity totalQty() const { return total_qty_; }
    Quantity openQty() const { return open_qty_; }
    Quantity filledQty() const { return total_qty_ - open_qty_; }
    Timestamp timestamp() const { return timestamp_; }

    bool isFilled() const { return open_qty_ == 0; }

    void reduceQty(Quantity qty) {
        if (qty > open_qty_) {
            open_qty_ = 0;
        } else {
            open_qty_ -= qty;
        }
    }

private:
    OrderId id_;
    std::string client_id_;
    Side side_;
    OrderType type_;
    Price price_;
    Quantity total_qty_;
    Quantity open_qty_;
    Timestamp timestamp_;
};

} // namespace minimarket
