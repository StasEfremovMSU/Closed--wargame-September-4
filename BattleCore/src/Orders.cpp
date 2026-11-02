#include "battle/Orders.h"
#include "battle/World.h"
namespace battle {
Status SubmitOrder(World &w, const Order &input) {
    auto *r = FindRegiment(w, input.recipient);
    if (!r)
        return Status::NotFound;
    Order o = input;
    if (!o.id)
        o.id = {w.next_order_id++};
    else
        w.next_order_id = std::max(w.next_order_id, o.id.value + 1);
    if (!Finite(o.point) || uint8_t(o.type) > uint8_t(OrderType::Rally))
        return Status::InvalidInput;
    if (o.type == OrderType::Attack) {
        bool target = true;
        if (o.target_unit) {
            auto *u = FindUnit(w, o.target_unit);
            target = u && u->alive && FindRegiment(w, u->regiment)->team != r->team;
        } else if (o.target_regiment) {
            auto *t = FindRegiment(w, o.target_regiment);
            target = t && !t->units.empty() && t->team != r->team;
        }
        if (!target) {
            o.status = OrderStatus::Failed;
            r->order = o;
            return Status::NotFound;
        }
    }
    o.status = OrderStatus::Pending;
    w.pending_orders.push_back(o);
    return Status::Ok;
}
} // namespace battle
