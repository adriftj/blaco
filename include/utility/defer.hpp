#pragma once

namespace bl {

template<typename Lambda>
struct defer : Lambda {
    ~defer() { Lambda::operator()(); }
};

template<typename Lambda>
defer(Lambda) -> defer<Lambda>;

} // namespace bl
