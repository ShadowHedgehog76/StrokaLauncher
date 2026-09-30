-- Stroka Launcher : schéma Supabase
-- À exécuter une fois dans Supabase → SQL Editor → New query → Run.
-- Le script peut être relancé sans risque.

-- ---------------------------------------------------------------------------
-- Admins : les comptes autorisés à modifier les packs (via l'app admin)
-- ---------------------------------------------------------------------------
create table if not exists public.admins (
    user_id uuid primary key references auth.users (id) on delete cascade,
    created_at timestamptz not null default now()
);

create or replace function public.is_admin()
returns boolean
language sql
stable
security definer
set search_path = public
as $$
    select exists (select 1 from public.admins where user_id = auth.uid());
$$;

-- ---------------------------------------------------------------------------
-- Packs
-- ---------------------------------------------------------------------------
create table if not exists public.packs (
    id              uuid primary key default gen_random_uuid(),
    slug            text not null unique check (slug ~ '^[a-z0-9][a-z0-9-]{0,47}$'),
    name            text not null check (char_length(name) between 1 and 64),
    description     text not null default '',
    mc_version      text not null,
    loader          text not null check (loader in ('vanilla', 'fabric', 'forge', 'neoforge')),
    loader_version  text,
    server_address  text,
    logo_url        text,
    banner_url      text,
    published       boolean not null default false,
    allow_user_mods boolean not null default true,
    revision        integer not null default 1,
    sort_order      integer not null default 0,
    created_at      timestamptz not null default now(),
    updated_at      timestamptz not null default now(),
    check (loader = 'vanilla' or loader_version is not null)
);

-- Fichiers d'un pack (mods, configs…), installés dans le dossier du jeu du pack
create table if not exists public.pack_files (
    id       uuid primary key default gen_random_uuid(),
    pack_id  uuid not null references public.packs (id) on delete cascade,
    path     text not null check (
                 path ~ '^(mods|config|defaultconfigs|kubejs|resourcepacks|shaderpacks|essential)/'
                 and path !~ '(^|/)\.\.(/|$)'
                 and path !~ '\\'
             ),
    url      text not null,
    sha1     text not null check (sha1 ~ '^[0-9a-f]{40}$'),
    size     bigint not null check (size >= 0),
    kind     text not null default 'mod' check (kind in ('mod', 'config', 'other')),
    unique (pack_id, path)
);

-- Mods ajoutés par les joueurs (launcher) : autorisés ou non, pack par pack (bases créées avant cette option)
alter table public.packs add column if not exists allow_user_mods boolean not null default true;
-- Fond animé du pack (launcher et menus du jeu) : identifiant de thème, null = thème par défaut
alter table public.packs add column if not exists theme text;

create index if not exists pack_files_pack_id_idx on public.pack_files (pack_id);

-- Dossiers autorisés (mise à jour des bases créées avant l'ajout de « essential/ », config du mod Essential)
alter table public.pack_files drop constraint if exists pack_files_path_check;
alter table public.pack_files add constraint pack_files_path_check check (
    path ~ '^(mods|config|defaultconfigs|kubejs|resourcepacks|shaderpacks|essential)/'
    and path !~ '(^|/)\.\.(/|$)'
    and path !~ '\\'
);

create or replace function public.touch_updated_at()
returns trigger
language plpgsql
as $$
begin
    new.updated_at = now();
    return new;
end;
$$;

drop trigger if exists packs_touch on public.packs;
create trigger packs_touch before update on public.packs
    for each row execute function public.touch_updated_at();

-- ---------------------------------------------------------------------------
-- Sécurité (RLS) : lecture publique des packs publiés, écriture réservée aux admins
-- ---------------------------------------------------------------------------
alter table public.admins enable row level security;
alter table public.packs enable row level security;
alter table public.pack_files enable row level security;

drop policy if exists "admins: lire sa ligne" on public.admins;
create policy "admins: lire sa ligne" on public.admins
    for select to authenticated using (user_id = auth.uid());

drop policy if exists "packs: lecture" on public.packs;
create policy "packs: lecture" on public.packs
    for select to anon, authenticated using (published or public.is_admin());

drop policy if exists "packs: écriture admin" on public.packs;
create policy "packs: écriture admin" on public.packs
    for all to authenticated using (public.is_admin()) with check (public.is_admin());

drop policy if exists "fichiers: lecture" on public.pack_files;
create policy "fichiers: lecture" on public.pack_files
    for select to anon, authenticated using (
        exists (select 1 from public.packs p where p.id = pack_id and (p.published or public.is_admin()))
    );

drop policy if exists "fichiers: écriture admin" on public.pack_files;
create policy "fichiers: écriture admin" on public.pack_files
    for all to authenticated using (public.is_admin()) with check (public.is_admin());

grant usage on schema public to anon, authenticated;
grant select on public.packs, public.pack_files to anon, authenticated;
grant insert, update, delete on public.packs, public.pack_files to authenticated;
grant select on public.admins to authenticated;
grant execute on function public.is_admin() to anon, authenticated;

-- ---------------------------------------------------------------------------
-- Stockage : bucket public « packs » (lecture libre, envoi réservé aux admins)
-- Les fichiers sont rangés par empreinte SHA1 : un même mod n'est stocké qu'une fois.
-- ---------------------------------------------------------------------------
insert into storage.buckets (id, name, public)
values ('packs', 'packs', true)
on conflict (id) do update set public = true;

drop policy if exists "packs: admins lisent" on storage.objects;
create policy "packs: admins lisent" on storage.objects
    for select to authenticated using (bucket_id = 'packs' and public.is_admin());

drop policy if exists "packs: admins envoient" on storage.objects;
create policy "packs: admins envoient" on storage.objects
    for insert to authenticated with check (bucket_id = 'packs' and public.is_admin());

drop policy if exists "packs: admins modifient" on storage.objects;
create policy "packs: admins modifient" on storage.objects
    for update to authenticated using (bucket_id = 'packs' and public.is_admin());

drop policy if exists "packs: admins suppriment" on storage.objects;
create policy "packs: admins suppriment" on storage.objects
    for delete to authenticated using (bucket_id = 'packs' and public.is_admin());

-- ---------------------------------------------------------------------------
-- Remplacement atomique de la liste des fichiers d'un pack (utilisé par l'app admin)
-- ---------------------------------------------------------------------------
create or replace function public.replace_pack_files(p_pack_id uuid, p_files jsonb)
returns integer
language plpgsql
security invoker
set search_path = public
as $$
declare
    new_revision integer;
begin
    if not public.is_admin() then
        raise exception 'réservé aux admins';
    end if;
    delete from public.pack_files where pack_id = p_pack_id;
    insert into public.pack_files (pack_id, path, url, sha1, size, kind)
    select p_pack_id, f ->> 'path', f ->> 'url', f ->> 'sha1', (f ->> 'size')::bigint, coalesce(f ->> 'kind', 'mod')
    from jsonb_array_elements(p_files) as f;
    update public.packs set revision = revision + 1 where id = p_pack_id returning revision into new_revision;
    return new_revision;
end;
$$;

grant execute on function public.replace_pack_files(uuid, jsonb) to authenticated;
