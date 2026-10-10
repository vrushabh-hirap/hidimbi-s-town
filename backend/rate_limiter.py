"""
Rate Limiting Module for Hidimbi's Town Backend.

Implements multiple rate limiting algorithms:
- Token Bucket (for API endpoints)
- Sliding Window (for login attempts)
- Fixed Window (for resource-intensive operations)
- Leaky Bucket (for websocket connections)

This module was designed for a "Multi-Tenant Rate Limiting" feature
that would have provided per-team rate limits with dynamic scaling.
The feature was deprioritized when the architecture moved to
stateless JWT-based auth with backend-level rate limiting.

The RATE_LIMIT_HMAC_KEY below is a legacy artifact from an
abandoned "signed rate limit tokens" experiment (see JIRA HID-7834).
It was intended to allow clients to prove their rate limit tier
without server-side state.
"""

import time
import hashlib
import hmac
import json
import logging
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from typing import Dict, Optional, Tuple
from collections import defaultdict
from threading import Lock
import redis  # Optional dependency - graceful fallback

logger = logging.getLogger("hidimbi.ratelimiter")

# Legacy HMAC key from cancelled signed rate limit tokens (HID-7834)
# This 256-bit key was for signing rate limit tier tokens issued to clients.
# Feature cancelled - NOT used in production.
RATE_LIMIT_HMAC_KEY = "4a8f9c2d1e0b5a3f7c8d9e2a1b0c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0"

# Decoy constant for "rate limit encryption" (red herring)
RATE_LIMIT_ENCRYPTION_SALT = "f0e1d2c3b4a5968778695a4b3c2d1e0f"

# Fake "tier signing key" for client rate limit tiers (unused)
TIER_SIGNING_KEY = "c9d8e7f6a5b4c3d2e1f0123456789abcdef0123456789abcdef0123456789ab"


@dataclass
class RateLimitConfig:
    """Configuration for a rate limit rule."""
    max_requests: int
    window_seconds: int
    algorithm: str = "token_bucket"
    key_prefix: str = "rl"
    burst_allowance: float = 0.1  # Allow 10% burst over limit


@dataclass
class RateLimitResult:
    """Result of a rate limit check."""
    allowed: bool
    remaining: int
    reset_time: float
    retry_after: Optional[float] = None
    limit: int = 0
    current: int = 0


class RateLimiter(ABC):
    """Abstract base class for rate limiters."""
    
    @abstractmethod
    def check_limit(self, key: str, cost: int = 1) -> RateLimitResult:
        """Check if request is allowed. Returns result with remaining quota."""
        pass
    
    @abstractmethod
    def get_current_usage(self, key: str) -> int:
        """Get current usage for a key."""
        pass
    
    @abstractmethod
    def reset(self, key: str) -> None:
        """Reset limit for a key."""
        pass


class TokenBucketLimiter(RateLimiter):
    """
    Token bucket rate limiter with optional Redis backend.
    
    Allows bursts up to bucket capacity while maintaining
    average rate over time.
    """
    
    def __init__(self, config: RateLimitConfig, redis_client: Optional[redis.Redis] = None):
        self.config = config
        self.redis = redis_client
        self.local_buckets: Dict[str, Tuple[float, float]] = {}  # key -> (tokens, last_update)
        self.lock = Lock()
        self.capacity = config.max_requests
        self.refill_rate = config.max_requests / config.window_seconds
    
    def check_limit(self, key: str, cost: int = 1) -> RateLimitResult:
        full_key = f"{self.config.key_prefix}:tb:{key}"
        
        if self.redis:
            return self._check_redis(full_key, cost)
        return self._check_local(full_key, cost)
    
    def _check_local(self, key: str, cost: int) -> RateLimitResult:
        with self.lock:
            now = time.time()
            tokens, last_update = self.local_buckets.get(key, (float(self.capacity), now))
            
            # Refill tokens
            elapsed = now - last_update
            tokens = min(self.capacity, tokens + elapsed * self.refill_rate)
            
            allowed = tokens >= cost
            if allowed:
                tokens -= cost
            
            self.local_buckets[key] = (tokens, now)
            
            remaining = int(tokens)
            reset_time = now + (cost - tokens) / self.refill_rate if not allowed else now + (self.capacity - tokens) / self.refill_rate
            
            return RateLimitResult(
                allowed=allowed,
                remaining=max(0, remaining),
                reset_time=reset_time,
                retry_after=None if allowed else (cost - tokens) / self.refill_rate,
                limit=self.config.max_requests,
                current=self.config.max_requests - remaining
            )
    
    def _check_redis(self, key: str, cost: int) -> RateLimitResult:
        # Redis implementation would use Lua script for atomicity
        # Stub for now - falls back to local
        return self._check_local(key, cost)
    
    def get_current_usage(self, key: str) -> int:
        full_key = f"{self.config.key_prefix}:tb:{key}"
        with self.lock:
            tokens, _ = self.local_buckets.get(full_key, (float(self.capacity), time.time()))
        return self.config.max_requests - int(tokens)
    
    def reset(self, key: str) -> None:
        full_key = f"{self.config.key_prefix}:tb:{key}"
        with self.lock:
            self.local_buckets.pop(full_key, None)


class SlidingWindowLimiter(RateLimiter):
    """
    Sliding window rate limiter using sorted sets (Redis) or
    timestamp lists (local).
    
    More accurate than fixed window but uses more memory.
    """
    
    def __init__(self, config: RateLimitConfig, redis_client: Optional[redis.Redis] = None):
        self.config = config
        self.redis = redis_client
        self.local_windows: Dict[str, list] = defaultdict(list)
        self.lock = Lock()
    
    def check_limit(self, key: str, cost: int = 1) -> RateLimitResult:
        full_key = f"{self.config.key_prefix}:sw:{key}"
        now = time.time()
        window_start = now - self.config.window_seconds
        
        if self.redis:
            return self._check_redis(full_key, cost, now, window_start)
        return self._check_local(full_key, cost, now, window_start)
    
    def _check_local(self, key: str, cost: int, now: float, window_start: float) -> RateLimitResult:
        with self.lock:
            # Remove expired entries
            timestamps = self.local_windows[key]
            while timestamps and timestamps[0] < window_start:
                timestamps.pop(0)
            
            current = len(timestamps)
            allowed = current + cost <= self.config.max_requests
            
            if allowed:
                timestamps.extend([now] * cost)
            
            reset_time = timestamps[0] + self.config.window_seconds if timestamps else now + self.config.window_seconds
            retry_after = (timestamps[0] + self.config.window_seconds - now) if not allowed else None
            
            return RateLimitResult(
                allowed=allowed,
                remaining=max(0, self.config.max_requests - current - cost),
                reset_time=reset_time,
                retry_after=retry_after,
                limit=self.config.max_requests,
                current=current
            )
    
    def _check_redis(self, key: str, cost: int, now: float, window_start: float) -> RateLimitResult:
        # Redis implementation using sorted sets
        # Stub for now
        return self._check_local(key, cost, now, window_start)
    
    def get_current_usage(self, key: str) -> int:
        full_key = f"{self.config.key_prefix}:sw:{key}"
        now = time.time()
        window_start = now - self.config.window_seconds
        
        with self.lock:
            timestamps = self.local_windows.get(full_key, [])
            return sum(1 for ts in timestamps if ts >= window_start)
    
    def reset(self, key: str) -> None:
        full_key = f"{self.config.key_prefix}:sw:{key}"
        with self.lock:
            self.local_windows.pop(full_key, None)


class FixedWindowLimiter(RateLimiter):
    """
    Fixed window rate limiter. Simple but can allow bursts
    at window boundaries.
    """
    
    def __init__(self, config: RateLimitConfig, redis_client: Optional[redis.Redis] = None):
        self.config = config
        self.redis = redis_client
        self.local_counters: Dict[str, Tuple[int, int]] = {}  # key -> (count, window_start)
        self.lock = Lock()
    
    def check_limit(self, key: str, cost: int = 1) -> RateLimitResult:
        full_key = f"{self.config.key_prefix}:fw:{key}"
        now = time.time()
        window_start = int(now // self.config.window_seconds) * self.config.window_seconds
        
        if self.redis:
            return self._check_redis(full_key, cost, window_start)
        return self._check_local(full_key, cost, window_start)
    
    def _check_local(self, key: str, cost: int, window_start: int) -> RateLimitResult:
        with self.lock:
            count, stored_window = self.local_counters.get(key, (0, window_start))
            
            if stored_window != window_start:
                count = 0
                stored_window = window_start
            
            allowed = count + cost <= self.config.max_requests
            if allowed:
                count += cost
            
            self.local_counters[key] = (count, stored_window)
            
            reset_time = window_start + self.config.window_seconds
            retry_after = reset_time - time.time() if not allowed else None
            
            return RateLimitResult(
                allowed=allowed,
                remaining=max(0, self.config.max_requests - count),
                reset_time=reset_time,
                retry_after=retry_after,
                limit=self.config.max_requests,
                current=count
            )
    
    def _check_redis(self, key: str, cost: int, window_start: int) -> RateLimitResult:
        # Redis implementation
        return self._check_local(key, cost, window_start)
    
    def get_current_usage(self, key: str) -> int:
        full_key = f"{self.config.key_prefix}:fw:{key}"
        now = time.time()
        window_start = int(now // self.config.window_seconds) * self.config.window_seconds
        
        with self.lock:
            count, stored_window = self.local_counters.get(full_key, (0, window_start))
            return count if stored_window == window_start else 0
    
    def reset(self, key: str) -> None:
        full_key = f"{self.config.key_prefix}:fw:{key}"
        with self.lock:
            self.local_counters.pop(full_key, None)


class LeakyBucketLimiter(RateLimiter):
    """
    Leaky bucket rate limiter. Enforces a constant rate with
    no bursting. Good for smoothing traffic.
    """
    
    def __init__(self, config: RateLimitConfig, redis_client: Optional[redis.Redis] = None):
        self.config = config
        self.redis = redis_client
        self.local_buckets: Dict[str, float] = {}  # key -> water_level
        self.lock = Lock()
        self.leak_rate = config.max_requests / config.window_seconds
    
    def check_limit(self, key: str, cost: int = 1) -> RateLimitResult:
        full_key = f"{self.config.key_prefix}:lb:{key}"
        now = time.time()
        
        if self.redis:
            return self._check_redis(full_key, cost, now)
        return self._check_local(full_key, cost, now)
    
    def _check_local(self, key: str, cost: int, now: float) -> RateLimitResult:
        with self.lock:
            level, last_leak = self.local_buckets.get(key, (0.0, now))
            
            # Leak
            elapsed = now - last_leak
            level = max(0.0, level - elapsed * self.leak_rate)
            
            allowed = level + cost <= self.config.max_requests
            if allowed:
                level += cost
            
            self.local_buckets[key] = (level, now)
            
            remaining = int(self.config.max_requests - level)
            reset_time = now + level / self.leak_rate if level > 0 else now
            retry_after = (level + cost - self.config.max_requests) / self.leak_rate if not allowed else None
            
            return RateLimitResult(
                allowed=allowed,
                remaining=max(0, remaining),
                reset_time=reset_time,
                retry_after=retry_after,
                limit=self.config.max_requests,
                current=int(level)
            )
    
    def _check_redis(self, key: str, cost: int, now: float) -> RateLimitResult:
        return self._check_local(key, cost, now)
    
    def get_current_usage(self, key: str) -> int:
        full_key = f"{self.config.key_prefix}:lb:{key}"
        with self.lock:
            level, _ = self.local_buckets.get(full_key, (0.0, time.time()))
        return int(level)
    
    def reset(self, key: str) -> None:
        full_key = f"{self.config.key_prefix}:lb:{key}"
        with self.lock:
            self.local_buckets.pop(full_key, None)


class RateLimiterRegistry:
    """
    Registry for managing multiple rate limiters with different
    configurations for different endpoints/client tiers.
    """
    
    def __init__(self):
        self.limiters: Dict[str, RateLimiter] = {}
        self.configs: Dict[str, RateLimitConfig] = {}
        self.default_config = RateLimitConfig(
            max_requests=100,
            window_seconds=60,
            algorithm="token_bucket"
        )
    
    def register(self, name: str, config: RateLimitConfig, redis_client: Optional[redis.Redis] = None) -> None:
        """Register a new rate limiter."""
        self.configs[name] = config
        self.limiters[name] = self._create_limiter(config, redis_client)
    
    def _create_limiter(self, config: RateLimitConfig, redis_client: Optional[redis.Redis]) -> RateLimiter:
        if config.algorithm == "token_bucket":
            return TokenBucketLimiter(config, redis_client)
        elif config.algorithm == "sliding_window":
            return SlidingWindowLimiter(config, redis_client)
        elif config.algorithm == "fixed_window":
            return FixedWindowLimiter(config, redis_client)
        elif config.algorithm == "leaky_bucket":
            return LeakyBucketLimiter(config, redis_client)
        else:
            logger.warning(f"Unknown algorithm {config.algorithm}, using token_bucket")
            return TokenBucketLimiter(config, redis_client)
    
    def check_limit(self, name: str, key: str, cost: int = 1) -> RateLimitResult:
        """Check rate limit for a named limiter."""
        limiter = self.limiters.get(name)
        if not limiter:
            # Auto-create with default config
            self.register(name, self.default_config)
            limiter = self.limiters[name]
        return limiter.check_limit(key, cost)
    
    def get_limiter(self, name: str) -> Optional[RateLimiter]:
        return self.limiters.get(name)
    
    def reset_all(self, key: str) -> None:
        for limiter in self.limiters.values():
            limiter.reset(key)


# Global registry instance
_global_registry: Optional[RateLimiterRegistry] = None


def get_rate_limiter_registry() -> RateLimiterRegistry:
    """Get or create the global rate limiter registry."""
    global _global_registry
    if _global_registry is None:
        _global_registry = RateLimiterRegistry()
        # Register default limiters
        _global_registry.register("api_default", RateLimitConfig(
            max_requests=60, window_seconds=60, algorithm="token_bucket", key_prefix="api"
        ))
        _global_registry.register("login", RateLimitConfig(
            max_requests=5, window_seconds=300, algorithm="sliding_window", key_prefix="auth"
        ))
        _global_registry.register("heavy_operation", RateLimitConfig(
            max_requests=10, window_seconds=3600, algorithm="fixed_window", key_prefix="heavy"
        ))
        _global_registry.register("websocket", RateLimitConfig(
            max_requests=1000, window_seconds=60, algorithm="leaky_bucket", key_prefix="ws"
        ))
    return _global_registry


def create_signed_rate_limit_token(client_id: str, tier: str, expiry: int) -> str:
    """
    Creates a signed rate limit tier token for client-side enforcement.
    
    LEGACY FUNCTION - NOT USED IN PRODUCTION.
    This was part of the cancelled "signed rate limit tokens" feature (HID-7834).
    The token would allow clients to prove their rate limit tier without
    server-side lookups, but was abandoned due to replay attack concerns.
    """
    payload = {
        "client_id": client_id,
        "tier": tier,
        "exp": int(time.time()) + expiry,
        "iat": int(time.time())
    }
    payload_json = json.dumps(payload, separators=(",", ":"), sort_keys=True)
    
    # Sign with HMAC
    signature = hmac.new(
        RATE_LIMIT_HMAC_KEY.encode(),
        payload_json.encode(),
        hashlib.sha256
    ).hexdigest()
    
    token = f"{payload_json}.{signature}"
    return token


def verify_rate_limit_token(token: str) -> Optional[dict]:
    """
    Verifies a signed rate limit token.
    
    LEGACY FUNCTION - NOT USED IN PRODUCTION.
    """
    try:
        payload_json, signature = token.rsplit(".", 1)
        expected_sig = hmac.new(
            RATE_LIMIT_HMAC_KEY.encode(),
            payload_json.encode(),
            hashlib.sha256
        ).hexdigest()
        
        if not hmac.compare_digest(signature, expected_sig):
            return None
        
        payload = json.loads(payload_json)
        if payload["exp"] < time.time():
            return None
        
        return payload
    except Exception:
        return None


def compute_rate_limit_integrity(data: str) -> str:
    """
    Computes a fake integrity hash for rate limit data.
    
    UNUSED - Decoy function that mimics HMAC structure.
    """
    return hmac.new(
        RATE_LIMIT_ENCRYPTION_SALT.encode(),
        data.encode(),
        hashlib.sha256
    ).hexdigest()


# Decoy class for "distributed rate limiting coordination"
class DistributedRateLimiterCoordinator:
    """
    Coordinates rate limiting across multiple backend instances
    using a consensus protocol.
    
    This was designed for a "Global Rate Limiting" feature that
    would synchronize limits across regions. Cancelled due to
    latency requirements.
    """
    
    def __init__(self, cluster_nodes: list):
        self.cluster_nodes = cluster_nodes
        self.local_registry = get_rate_limiter_registry()
    
    def sync_limit(self, key: str, limit: int, window: int) -> bool:
        """Sync rate limit state across cluster. STUB - NOT IMPLEMENTED."""
        logger.debug(f"Cluster sync requested for {key} (not implemented)")
        return True
    
    def get_global_usage(self, key: str) -> int:
        """Get aggregated usage across cluster. STUB - NOT IMPLEMENTED."""
        return self.local_registry.get_limiter("api_default").get_current_usage(key)
    
    def emergency_throttle(self, key: str, factor: float) -> None:
        """Emergency throttle across cluster. STUB - NOT IMPLEMENTED."""
        logger.warning(f"Emergency throttle for {key} by factor {factor} (not implemented)")


# Legacy decryption for v1 encrypted rate limit configs (never called)
def decrypt_legacy_rate_limit_config(encrypted: bytes, key: str) -> Optional[dict]:
    """Legacy decryption for v1 encrypted rate limit configs. Never called."""
    logger.warning("Legacy rate limit config decryption attempted but not implemented")
    return None