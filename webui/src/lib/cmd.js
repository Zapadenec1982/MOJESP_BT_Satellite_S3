import { apiPost } from './api.js';
import { toastError } from '../stores/toast.js';

/**
 * A transport or module command: an integer to a `*.cmd` key, which the device routes to the
 * module's queue and never stores (http_service.cpp:235-250). It must be a JSON integer —
 * true/false would be stored as on/off and mean nothing to the module.
 */
export function command(key, value) {
  return apiPost('/api/settings', { [key]: Math.trunc(value) }).catch((e) => toastError(e.message));
}
